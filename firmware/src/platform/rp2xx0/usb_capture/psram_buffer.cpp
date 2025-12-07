#include "psram_buffer.h"
#include <string.h>

// Global buffer (static allocation for now, FRAM later)
psram_buffer_t g_psram_buffer;

void psram_buffer_init() {
    // Zero out entire structure
    memset(&g_psram_buffer, 0, sizeof(psram_buffer_t));

    // Set magic number for validation
    g_psram_buffer.header.magic = PSRAM_MAGIC;
    g_psram_buffer.header.write_index = 0;
    g_psram_buffer.header.read_index = 0;
    g_psram_buffer.header.buffer_count = 0;
    g_psram_buffer.header.total_written = 0;
    g_psram_buffer.header.total_transmitted = 0;
    g_psram_buffer.header.dropped_buffers = 0;
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

    // Increment available buffer count (Core0 will read this)
    g_psram_buffer.header.buffer_count++;
    g_psram_buffer.header.total_written++;

    return true;
}

bool psram_buffer_has_data() {
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

    // Get current read slot
    uint32_t slot = g_psram_buffer.header.read_index;

    // Copy buffer data from PSRAM slot
    memcpy(buffer, &g_psram_buffer.slots[slot], sizeof(psram_keystroke_buffer_t));

    // Update read index (circular buffer, wrap at PSRAM_BUFFER_SLOTS)
    g_psram_buffer.header.read_index = (slot + 1) % PSRAM_BUFFER_SLOTS;

    // Decrement available buffer count
    g_psram_buffer.header.buffer_count--;
    g_psram_buffer.header.total_transmitted++;

    return true;
}

uint32_t psram_buffer_get_count() {
    return g_psram_buffer.header.buffer_count;
}
