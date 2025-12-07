/**
 * @file keystroke_queue.cpp
 * @brief Keystroke event queue implementation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "keystroke_queue.h"
#include "pico/time.h"
#include <string.h>

extern "C" {

void keystroke_queue_init(keystroke_queue_t *queue)
{
    memset(queue, 0, sizeof(keystroke_queue_t));
    queue->write_index = 0;
    queue->read_index = 0;
    queue->dropped_count = 0;
    queue->total_pushed = 0;
}

void keystroke_queue_reset(keystroke_queue_t *queue)
{
    queue->write_index = 0;
    queue->read_index = 0;
    /* Keep dropped_count and total_pushed for statistics */
}

bool keystroke_queue_push(keystroke_queue_t *queue, const keystroke_event_t *event)
{
    uint32_t next_write = (queue->write_index + 1) & KEYSTROKE_QUEUE_MASK;

    /* Check if queue is full */
    if (next_write == queue->read_index)
    {
        queue->dropped_count++;
        return false;
    }

    /* Add queue timestamp and calculate latency */
    keystroke_event_t modified_event = *event;
    modified_event.queue_timestamp_us = time_us_64();

    if (modified_event.capture_timestamp_us > 0 &&
        modified_event.queue_timestamp_us >= modified_event.capture_timestamp_us)
    {
        uint64_t latency = modified_event.queue_timestamp_us - modified_event.capture_timestamp_us;
        modified_event.processing_latency_us = (latency > UINT32_MAX) ? UINT32_MAX : (uint32_t)latency;
    }

    /* Write event */
    queue->events[queue->write_index] = modified_event;

    /* Update write index */
    queue->write_index = next_write;
    queue->total_pushed++;

    return true;
}

bool keystroke_queue_pop(keystroke_queue_t *queue, keystroke_event_t *event)
{
    /* Check if queue is empty */
    if (queue->read_index == queue->write_index)
    {
        return false;
    }

    /* Read event */
    *event = queue->events[queue->read_index];

    /* Update read index */
    queue->read_index = (queue->read_index + 1) & KEYSTROKE_QUEUE_MASK;

    return true;
}

bool keystroke_queue_is_empty(const keystroke_queue_t *queue)
{
    return queue->read_index == queue->write_index;
}

uint32_t keystroke_queue_count(const keystroke_queue_t *queue)
{
    uint32_t write_idx = queue->write_index;
    uint32_t read_idx = queue->read_index;

    if (write_idx >= read_idx)
    {
        return write_idx - read_idx;
    }
    else
    {
        return (KEYSTROKE_QUEUE_SIZE - read_idx) + write_idx;
    }
}

uint32_t keystroke_queue_get_dropped_count(const keystroke_queue_t *queue)
{
    return queue->dropped_count;
}

} // extern "C"
