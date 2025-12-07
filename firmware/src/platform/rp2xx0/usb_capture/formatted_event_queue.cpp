#include "formatted_event_queue.h"
#include <string.h>

void formatted_queue_init(formatted_event_queue_t *queue) {
    if (!queue) return;

    memset(queue->events, 0, sizeof(queue->events));
    queue->write_index = 0;
    queue->read_index = 0;
}

bool formatted_queue_push(formatted_event_queue_t *queue, const formatted_event_t *event) {
    if (!queue || !event) return false;

    // Check if queue is full
    uint32_t next_write = (queue->write_index + 1) % FORMATTED_QUEUE_SIZE;
    if (next_write == queue->read_index) {
        return false;  // Queue full
    }

    // Copy event data
    formatted_event_t *dest = &queue->events[queue->write_index];
    strncpy(dest->text, event->text, MAX_FORMATTED_LEN - 1);
    dest->text[MAX_FORMATTED_LEN - 1] = '\0';  // Ensure null termination
    dest->timestamp_us = event->timestamp_us;
    dest->core_id = event->core_id;

    // Update write index (atomic for single producer)
    queue->write_index = next_write;

    return true;
}

bool formatted_queue_pop(formatted_event_queue_t *queue, formatted_event_t *event) {
    if (!queue || !event) return false;

    // Check if queue is empty
    if (queue->read_index == queue->write_index) {
        return false;  // Queue empty
    }

    // Copy event data
    const formatted_event_t *src = &queue->events[queue->read_index];
    strncpy(event->text, src->text, MAX_FORMATTED_LEN - 1);
    event->text[MAX_FORMATTED_LEN - 1] = '\0';  // Ensure null termination
    event->timestamp_us = src->timestamp_us;
    event->core_id = src->core_id;

    // Update read index (atomic for single consumer)
    queue->read_index = (queue->read_index + 1) % FORMATTED_QUEUE_SIZE;

    return true;
}
