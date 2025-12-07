#ifndef FORMATTED_EVENT_QUEUE_H
#define FORMATTED_EVENT_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Queue configuration
#define FORMATTED_QUEUE_SIZE 64
#define MAX_FORMATTED_LEN 128

/**
 * Formatted event structure
 * Contains pre-formatted text ready for logging/transmission
 * Created on Core1 to offload formatting work from Core0
 */
typedef struct {
    char text[MAX_FORMATTED_LEN];     // Pre-formatted event string
    uint64_t timestamp_us;             // Capture timestamp
    uint8_t core_id;                   // Which core formatted this (for debugging)
} formatted_event_t;

/**
 * Lock-free circular queue for formatted events
 * Same pattern as keystroke_queue for Core0↔Core1 communication
 */
typedef struct {
    formatted_event_t events[FORMATTED_QUEUE_SIZE];
    volatile uint32_t write_index;     // Core1 writes here
    volatile uint32_t read_index;      // Core0 reads here
} formatted_event_queue_t;

/**
 * Initialize formatted event queue
 * @param queue Pointer to queue structure
 */
void formatted_queue_init(formatted_event_queue_t *queue);

/**
 * Push formatted event to queue (Core1 → Core0)
 * Lock-free operation safe for single producer (Core1)
 *
 * @param queue Pointer to queue structure
 * @param event Event to push
 * @return true if pushed, false if queue full
 */
bool formatted_queue_push(formatted_event_queue_t *queue, const formatted_event_t *event);

/**
 * Pop formatted event from queue (Core0 reads)
 * Lock-free operation safe for single consumer (Core0)
 *
 * @param queue Pointer to queue structure
 * @param event Output buffer for event
 * @return true if event retrieved, false if queue empty
 */
bool formatted_queue_pop(formatted_event_queue_t *queue, formatted_event_t *event);

/**
 * Check if queue is empty
 * @param queue Pointer to queue structure
 * @return true if empty
 */
static inline bool formatted_queue_is_empty(const formatted_event_queue_t *queue) {
    return queue->read_index == queue->write_index;
}

/**
 * Check if queue is full
 * @param queue Pointer to queue structure
 * @return true if full
 */
static inline bool formatted_queue_is_full(const formatted_event_queue_t *queue) {
    return ((queue->write_index + 1) % FORMATTED_QUEUE_SIZE) == queue->read_index;
}

/**
 * Get number of events in queue
 * @param queue Pointer to queue structure
 * @return Number of events available
 */
static inline uint32_t formatted_queue_count(const formatted_event_queue_t *queue) {
    uint32_t write = queue->write_index;
    uint32_t read = queue->read_index;
    if (write >= read) {
        return write - read;
    } else {
        return FORMATTED_QUEUE_SIZE - (read - write);
    }
}

#ifdef __cplusplus
}
#endif

#endif // FORMATTED_EVENT_QUEUE_H
