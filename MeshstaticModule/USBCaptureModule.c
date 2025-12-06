/**
 * @file USBCaptureModule.c
 * @brief Implementation of independent Core 1 USB capture module
 *
 * This module runs exclusively on Core 1 and provides a simple interface:
 * - Capture USB keystrokes using PIO hardware
 * - Store in lock-free queue
 * - Other modules consume from queue (Core 0 or Core 1)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "USBCaptureModule.h"
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Lock-Free Ring Buffer Queue (Core1→Core0 safe)
 * ============================================================================ */

/**
 * @brief Lock-free ring buffer for keystroke events
 *
 * Thread-safe for single-producer (Core 1), single-consumer (Core 0) pattern.
 * Uses atomic head/tail pointers with power-of-2 size for efficient modulo.
 */
typedef struct {
    keystroke_event_t events[USB_CAPTURE_QUEUE_SIZE];
    volatile uint32_t head;     /**< Write index (Core 1 only) */
    volatile uint32_t tail;     /**< Read index (Core 0 only) */
} keystroke_queue_lockfree_t;

/* ============================================================================
 * Private State
 * ============================================================================ */

/** Keystroke event queue (lock-free ring buffer) */
static keystroke_queue_lockfree_t g_queue;

/** Module configuration */
static usb_capture_config_t g_config;

/** Module state */
static bool g_initialized = false;
static bool g_running = false;

/** Statistics (volatile for multi-core access) */
static volatile usb_capture_stats_t g_stats;

/* ============================================================================
 * Lock-Free Queue Implementation
 * ============================================================================ */

/**
 * @brief Initialize queue
 */
static void queue_init(keystroke_queue_lockfree_t* queue)
{
    memset((void*)queue, 0, sizeof(keystroke_queue_lockfree_t));
}

/**
 * @brief Push event to queue (Core 1 only)
 *
 * @return true if pushed successfully, false if queue full
 */
static bool queue_push(keystroke_queue_lockfree_t* queue, const keystroke_event_t* event)
{
    uint32_t head = queue->head;
    uint32_t next_head = (head + 1) & (USB_CAPTURE_QUEUE_SIZE - 1);

    /* Check if queue full */
    if (next_head == queue->tail) {
        return false;  /* Queue full */
    }

    /* Copy event */
    memcpy((void*)&queue->events[head], event, sizeof(keystroke_event_t));

    /* Update head (atomic write) */
    queue->head = next_head;

    return true;
}

/**
 * @brief Pop event from queue (Core 0 only)
 *
 * @return true if popped successfully, false if queue empty
 */
static bool queue_pop(keystroke_queue_lockfree_t* queue, keystroke_event_t* event)
{
    uint32_t tail = queue->tail;

    /* Check if queue empty */
    if (tail == queue->head) {
        return false;  /* Queue empty */
    }

    /* Copy event */
    memcpy(event, (void*)&queue->events[tail], sizeof(keystroke_event_t));

    /* Update tail (atomic write) */
    queue->tail = (tail + 1) & (USB_CAPTURE_QUEUE_SIZE - 1);

    return true;
}

/**
 * @brief Peek at next event without removing (Core 0/Core 1)
 *
 * @return true if event available, false if queue empty
 */
static bool queue_peek(keystroke_queue_lockfree_t* queue, keystroke_event_t* event)
{
    uint32_t tail = queue->tail;

    /* Check if queue empty */
    if (tail == queue->head) {
        return false;  /* Queue empty */
    }

    /* Copy event without updating tail */
    memcpy(event, (void*)&queue->events[tail], sizeof(keystroke_event_t));

    return true;
}

/**
 * @brief Get queue count (Core 0/Core 1)
 *
 * @return Number of events in queue
 */
static uint32_t queue_count(keystroke_queue_lockfree_t* queue)
{
    uint32_t head = queue->head;
    uint32_t tail = queue->tail;

    if (head >= tail) {
        return head - tail;
    } else {
        return USB_CAPTURE_QUEUE_SIZE - (tail - head);
    }
}

/* ============================================================================
 * USB Packet Processing (Simplified)
 * ============================================================================ */

/**
 * @brief Decode USB packet to keystroke event
 *
 * This is a simplified version. In real integration, this would call:
 * - packet_processor_core1_process_packet()
 * - keyboard_decoder_core1_process_packet()
 *
 * @param packet_data Raw USB packet data
 * @param packet_size Packet size in bytes
 * @param event Output: decoded keystroke event
 * @return true if decode successful
 */
static bool decode_usb_packet(const uint8_t* packet_data,
                              uint32_t packet_size,
                              keystroke_event_t* event,
                              uint32_t timestamp_us)
{
    /* This is a PLACEHOLDER for demonstration */
    /* Real implementation would call existing decoders */

    if (!packet_data || !event || packet_size < 8) {
        return false;
    }

    /* Example: HID keyboard packet format:
     * Byte 0: Modifier byte (Shift, Ctrl, Alt, etc.)
     * Byte 1: Reserved
     * Byte 2-7: Scancode array (up to 6 simultaneous keys)
     */

    event->modifier = packet_data[0];
    event->scancode = packet_data[2];  /* First scancode */
    event->timestamp_us = timestamp_us;
    event->type = KEYSTROKE_TYPE_CHAR;

    /* Convert scancode to ASCII character (simplified) */
    if (event->scancode >= 0x04 && event->scancode <= 0x1D) {
        /* Letters: A-Z */
        event->character = 'a' + (event->scancode - 0x04);
        if (event->modifier & 0x02) {  /* Shift */
            event->character = 'A' + (event->scancode - 0x04);
        }
    } else if (event->scancode == 0x2C) {
        /* Space */
        event->character = ' ';
    } else if (event->scancode == 0x28) {
        /* Enter */
        event->character = '\n';
        event->type = KEYSTROKE_TYPE_ENTER;
    } else {
        /* Unknown key */
        event->character = '?';
    }

    return true;
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

bool usb_capture_module_init(const usb_capture_config_t* config)
{
    if (g_initialized) {
        return true;  /* Already initialized */
    }

    if (!config) {
        return false;
    }

    /* Save configuration */
    memcpy(&g_config, config, sizeof(usb_capture_config_t));

    /* Initialize queue */
    queue_init(&g_queue);

    /* Reset statistics */
    memset((void*)&g_stats, 0, sizeof(usb_capture_stats_t));

    g_initialized = true;

    printf("[USB_CAPTURE_MODULE] Initialized (D+=%u, D-=%u, speed=%s)\n",
           g_config.dp_pin,
           g_config.dm_pin,
           g_config.full_speed_mode ? "FULL" : "LOW");

    return true;
}

bool usb_capture_module_start(void)
{
    if (!g_initialized) {
        return false;
    }

    if (g_running) {
        return true;  /* Already running */
    }

    /* In real implementation, this would:
     * 1. Configure PIO for USB capture
     * 2. Start PIO state machines
     * 3. Enable DMA (if used)
     */

    g_running = true;

    printf("[USB_CAPTURE_MODULE] Capture started\n");

    return true;
}

uint32_t usb_capture_module_process(void)
{
    if (!g_initialized || !g_running) {
        return 0;
    }

    uint32_t events_added = 0;

    /* Process up to MAX_PROCESS_PER_LOOP events per iteration */
    for (uint32_t i = 0; i < USB_CAPTURE_MAX_PROCESS_PER_LOOP; i++) {
        /* In real implementation, this would:
         * 1. Check if PIO FIFO has data (pio_sm_is_rx_fifo_empty)
         * 2. Read from PIO FIFO (pio_sm_get)
         * 3. Accumulate into packet buffer
         * 4. When packet complete, decode it
         * 5. Push keystroke event to queue
         */

        /* PLACEHOLDER: For demonstration, we simulate having no data */
        bool pio_has_data = false;  /* Replace with: !pio_sm_is_rx_fifo_empty(...) */

        if (!pio_has_data) {
            break;  /* No more data available */
        }

        /* Read and decode USB packet */
        uint8_t packet_buffer[64];
        uint32_t packet_size = 8;  /* Placeholder */
        uint32_t timestamp_us = 0;  /* Placeholder - use time_us_64() */

        keystroke_event_t event;
        if (decode_usb_packet(packet_buffer, packet_size, &event, timestamp_us)) {
            /* Push to queue */
            if (queue_push(&g_queue, &event)) {
                g_stats.events_captured++;
                g_stats.events_queued++;
                events_added++;
            } else {
                /* Queue overflow */
                g_stats.queue_overflows++;
            }

            g_stats.packets_processed++;
        } else {
            /* Decode error */
            g_stats.decode_errors++;
        }
    }

    /* Update current queue size */
    g_stats.current_queue_size = queue_count(&g_queue);

    return events_added;
}

void usb_capture_module_stop(void)
{
    if (!g_running) {
        return;
    }

    /* In real implementation, this would:
     * 1. Stop PIO state machines
     * 2. Disable DMA
     * 3. Clear FIFOs
     */

    g_running = false;

    printf("[USB_CAPTURE_MODULE] Capture stopped\n");
}

bool usb_capture_module_is_running(void)
{
    return g_running;
}

/* ============================================================================
 * Consumer Interface (Core 0 reads from queue)
 * ============================================================================ */

bool usb_capture_module_available(void)
{
    return (queue_count(&g_queue) > 0);
}

uint32_t usb_capture_module_get_count(void)
{
    return queue_count(&g_queue);
}

bool usb_capture_module_pop(keystroke_event_t* event)
{
    if (!event) {
        return false;
    }

    return queue_pop(&g_queue, event);
}

bool usb_capture_module_peek(keystroke_event_t* event)
{
    if (!event) {
        return false;
    }

    return queue_peek(&g_queue, event);
}

/* ============================================================================
 * Statistics
 * ============================================================================ */

void usb_capture_module_get_stats(usb_capture_stats_t* stats)
{
    if (!stats) {
        return;
    }

    /* Copy statistics (atomic reads for multi-core safety) */
    stats->events_captured = g_stats.events_captured;
    stats->events_queued = g_stats.events_queued;
    stats->queue_overflows = g_stats.queue_overflows;
    stats->decode_errors = g_stats.decode_errors;
    stats->packets_processed = g_stats.packets_processed;
    stats->current_queue_size = queue_count(&g_queue);
}

bool usb_capture_module_reset_stats(void)
{
    if (!g_initialized) {
        return false;
    }

    memset((void*)&g_stats, 0, sizeof(usb_capture_stats_t));
    return true;
}

void usb_capture_module_print_stats(void)
{
    usb_capture_stats_t stats;
    usb_capture_module_get_stats(&stats);

    printf("========== USB CAPTURE MODULE STATISTICS ==========\n");
    printf("Events Captured:    %u\n", stats.events_captured);
    printf("Events Queued:      %u\n", stats.events_queued);
    printf("Queue Overflows:    %u\n", stats.queue_overflows);
    printf("Decode Errors:      %u\n", stats.decode_errors);
    printf("Packets Processed:  %u\n", stats.packets_processed);
    printf("Current Queue Size: %u/%u\n", stats.current_queue_size, USB_CAPTURE_QUEUE_SIZE);
    printf("===================================================\n");
}
