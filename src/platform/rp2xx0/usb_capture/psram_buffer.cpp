/**
 * @file psram_buffer.cpp
 * @brief PSRAM ring buffer implementation
 *
 * Implementation of an 8-slot circular buffer for Core0↔Core1 communication.
 * Core1 writes complete keystroke buffers, Core0 reads and transmits them.
 *
 * Design Notes:
 * - Currently uses static RAM allocation
 * - Future: Replace with I2C FRAM for non-volatile storage
 * - Lock-free: Safe for single producer (Core1) / single consumer (Core0)
 * - Statistics: Tracks writes, reads, and buffer drops
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "psram_buffer.h"
#include <string.h>
#include "hardware/sync.h"  // For __dmb() memory barriers (v3.5)

/**
 * Global buffer instance
 * TODO: Migrate to I2C FRAM for:
 *  - Non-volatile persistence (survives power loss)
 *  - MB-scale capacity (vs KB for RAM)
 *  - Extreme endurance (10^14 write cycles)
 */
psram_buffer_t g_psram_buffer;

void psram_buffer_init() {
    // Zero out entire structure (includes new statistics fields)
    memset(&g_psram_buffer, 0, sizeof(psram_buffer_t));

    // Set magic number for validation
    g_psram_buffer.header.magic = PSRAM_MAGIC;
    g_psram_buffer.header.write_index = 0;
    g_psram_buffer.header.read_index = 0;
    g_psram_buffer.header.buffer_count = 0;
    g_psram_buffer.header.total_written = 0;
    g_psram_buffer.header.total_transmitted = 0;
    g_psram_buffer.header.dropped_buffers = 0;

    // Initialize new statistics fields (v3.5)
    g_psram_buffer.header.transmission_failures = 0;
    g_psram_buffer.header.buffer_overflows = 0;
    g_psram_buffer.header.psram_write_failures = 0;
    g_psram_buffer.header.retry_attempts = 0;
}

bool psram_buffer_write(const psram_keystroke_buffer_t *buffer) {
    if (!buffer) {
        return false;
    }

    // Check if buffer full (all 8 slots occupied)
    if (g_psram_buffer.header.buffer_count >= PSRAM_BUFFER_SLOTS) {
        g_psram_buffer.header.dropped_buffers++;
        return false;  // Buffer full, Core0 needs to transmit faster
    }

    // Get current write slot
    uint32_t slot = g_psram_buffer.header.write_index;

    // Copy buffer data to PSRAM slot
    memcpy(&g_psram_buffer.slots[slot], buffer, sizeof(psram_keystroke_buffer_t));

    // Update write index (circular buffer, wrap at PSRAM_BUFFER_SLOTS)
    g_psram_buffer.header.write_index = (slot + 1) % PSRAM_BUFFER_SLOTS;

    // Memory barrier: Ensure all writes complete before incrementing count
    // Critical for ARM Cortex-M33 cache coherency between Core0 and Core1
    __dmb();

    // Increment available buffer count (Core0 will read this)
    g_psram_buffer.header.buffer_count++;
    g_psram_buffer.header.total_written++;

    // Memory barrier: Ensure count increment is visible to Core0
    __dmb();

    return true;
}

bool psram_buffer_has_data() {
    // Memory barrier: Ensure we read the latest buffer_count from Core1
    __dmb();
    return g_psram_buffer.header.buffer_count > 0;
}

bool psram_buffer_read(psram_keystroke_buffer_t *buffer) {
    if (!buffer) {
        return false;
    }

    // Check if data available
    if (g_psram_buffer.header.buffer_count == 0) {
        return false;  // No data available
    }

    // Memory barrier: Ensure we see the latest data from Core1
    __dmb();

    // Get current read slot
    uint32_t slot = g_psram_buffer.header.read_index;

    // Copy buffer data from PSRAM slot
    memcpy(buffer, &g_psram_buffer.slots[slot], sizeof(psram_keystroke_buffer_t));

    // Update read index (circular buffer, wrap at PSRAM_BUFFER_SLOTS)
    g_psram_buffer.header.read_index = (slot + 1) % PSRAM_BUFFER_SLOTS;

    // Memory barrier: Ensure read completes before decrementing count
    __dmb();

    // Decrement available buffer count
    g_psram_buffer.header.buffer_count--;
    g_psram_buffer.header.total_transmitted++;

    // Memory barrier: Ensure count decrement is visible to Core1
    __dmb();

    return true;
}

uint32_t psram_buffer_get_count() {
    // Memory barrier: Ensure we read the latest count
    __dmb();
    return g_psram_buffer.header.buffer_count;
}
