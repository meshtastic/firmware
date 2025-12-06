/**
 * @file keystroke_queue.h
 * @brief Keystroke event queue for Core0-Core1 communication
 *
 * Lock-free ring buffer for passing decoded keystroke events from Core1
 * (producer) to Core0 (consumer). Designed for single producer/single consumer.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef KEYSTROKE_QUEUE_H
#define KEYSTROKE_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Ring buffer configuration
 *
 * Size is power of 2 for efficient modulo operations.
 * 1024 events * 32 bytes = 32KB memory footprint
 */
#define KEYSTROKE_QUEUE_SIZE 1024
#define KEYSTROKE_QUEUE_MASK (KEYSTROKE_QUEUE_SIZE - 1)

/**
 * @brief Keystroke event queue structure
 *
 * Lock-free single-producer/single-consumer ring buffer.
 * Volatile indices ensure visibility across cores.
 */
typedef struct {
    keystroke_event_t events[KEYSTROKE_QUEUE_SIZE];
    volatile uint32_t write_index;  /**< Producer write position */
    volatile uint32_t read_index;   /**< Consumer read position */
    volatile uint32_t dropped_count; /**< Events dropped due to queue full */
    volatile uint32_t total_pushed;  /**< Total events pushed */
} keystroke_queue_t;

/**
 * @brief Initialize the keystroke queue
 */
void keystroke_queue_init(keystroke_queue_t *queue);

/**
 * @brief Reset the keystroke queue (clear all events)
 */
void keystroke_queue_reset(keystroke_queue_t *queue);

/**
 * @brief Push a keystroke event to the queue (Core1 producer)
 *
 * @param queue Pointer to queue structure
 * @param event Pointer to event to push
 * @return true if event was pushed, false if queue was full
 */
bool keystroke_queue_push(keystroke_queue_t *queue, const keystroke_event_t *event);

/**
 * @brief Pop a keystroke event from the queue (Core0 consumer)
 *
 * @param queue Pointer to queue structure
 * @param event Pointer to store popped event
 * @return true if event was popped, false if queue was empty
 */
bool keystroke_queue_pop(keystroke_queue_t *queue, keystroke_event_t *event);

/**
 * @brief Check if queue is empty (Core0 consumer)
 *
 * @return true if queue is empty
 */
bool keystroke_queue_is_empty(const keystroke_queue_t *queue);

/**
 * @brief Get current queue occupancy (for diagnostics)
 *
 * @return Number of events currently in queue
 */
uint32_t keystroke_queue_count(const keystroke_queue_t *queue);

/**
 * @brief Get dropped event count (for diagnostics)
 *
 * @return Number of events dropped due to queue full
 */
uint32_t keystroke_queue_get_dropped_count(const keystroke_queue_t *queue);

/**
 * @brief Helper: Create character event
 */
static inline keystroke_event_t keystroke_event_create_char(
    char ch, uint8_t scancode, uint8_t modifier, uint64_t capture_timestamp_us)
{
    keystroke_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = KEYSTROKE_TYPE_CHAR;
    event.character = ch;
    event.scancode = scancode;
    event.modifier = modifier;
    event.capture_timestamp_us = capture_timestamp_us;
    return event;
}

/**
 * @brief Helper: Create special key event
 */
static inline keystroke_event_t keystroke_event_create_special(
    keystroke_type_t type, uint8_t scancode, uint64_t capture_timestamp_us)
{
    keystroke_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.scancode = scancode;
    event.capture_timestamp_us = capture_timestamp_us;
    return event;
}

/**
 * @brief Helper: Create error event
 */
static inline keystroke_event_t keystroke_event_create_error(
    uint32_t error_flags, uint64_t capture_timestamp_us)
{
    keystroke_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = KEYSTROKE_TYPE_ERROR;
    event.error_flags = error_flags;
    event.capture_timestamp_us = capture_timestamp_us;
    return event;
}

#ifdef __cplusplus
}
#endif
#endif /* KEYSTROKE_QUEUE_H */
