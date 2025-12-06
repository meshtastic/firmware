/**
 * @file meshstatic_batch.h
 * @brief Keystroke batch manager for CSV-based flash storage
 *
 * Purpose: Organize captured keystrokes into CSV-formatted batches with
 * a strict 200-byte file size limit per batch.
 *
 * Design Goals:
 * - CSV format for human readability and easy parsing
 * - 200-byte file limit for efficient flash storage
 * - Automatic batch flushing when size limit reached
 * - Independent module running on Core 1
 *
 * CSV Format:
 *   timestamp_us,scancode,modifier,character
 *   1234567890,0x04,0x00,a
 *   1234568000,0x05,0x02,B
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MESHSTATIC_BATCH_H
#define MESHSTATIC_BATCH_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants and Configuration
 * ============================================================================ */

/** Maximum CSV line length: "1234567890,0xFF,0xFF,X\n" = ~25 bytes */
#define MESHSTATIC_MAX_CSV_LINE_LENGTH      32

/** CSV header: "timestamp_us,scancode,modifier,character\n" = 42 bytes */
#define MESHSTATIC_CSV_HEADER_LENGTH        42

/** Maximum batch size in bytes (including header) */
#define MESHSTATIC_MAX_BATCH_SIZE           200

/** Calculated max keystrokes per batch: (200 - 42) / 32 = ~4 keystrokes */
#define MESHSTATIC_MAX_KEYSTROKES_PER_BATCH ((MESHSTATIC_MAX_BATCH_SIZE - MESHSTATIC_CSV_HEADER_LENGTH) / MESHSTATIC_MAX_CSV_LINE_LENGTH)

/** CSV buffer size (must fit entire batch) */
#define MESHSTATIC_CSV_BUFFER_SIZE          MESHSTATIC_MAX_BATCH_SIZE

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/**
 * @brief Single keystroke record (minimal memory footprint)
 */
typedef struct {
    uint32_t timestamp_us;      /**< Microsecond timestamp */
    uint8_t scancode;           /**< HID scancode */
    uint8_t modifier;           /**< Modifier flags (Shift, Ctrl, Alt, etc.) */
    char character;             /**< ASCII character */
} __attribute__((packed)) meshstatic_keystroke_t;

/**
 * @brief Batch metadata
 */
typedef struct {
    uint32_t batch_id;          /**< Unique batch identifier */
    uint32_t count;             /**< Number of keystrokes in batch */
    uint32_t csv_length;        /**< Current CSV string length (bytes) */
    uint32_t start_time_us;     /**< First keystroke timestamp */
    uint32_t end_time_us;       /**< Last keystroke timestamp */
    bool needs_flush;           /**< True if batch ready for flushing */
} meshstatic_batch_meta_t;

/**
 * @brief Batch buffer (holds keystrokes + CSV representation)
 */
typedef struct {
    meshstatic_batch_meta_t meta;                           /**< Batch metadata */
    meshstatic_keystroke_t keystrokes[MESHSTATIC_MAX_KEYSTROKES_PER_BATCH]; /**< Keystroke array */
    char csv_buffer[MESHSTATIC_CSV_BUFFER_SIZE];            /**< CSV string buffer */
} meshstatic_batch_t;

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * @brief Initialize batch system
 *
 * Resets batch metadata and prepares CSV buffer with header.
 *
 * @param batch Pointer to batch structure
 */
void meshstatic_batch_init(meshstatic_batch_t* batch);

/**
 * @brief Add keystroke to current batch
 *
 * Appends keystroke to batch and updates CSV buffer.
 * Automatically sets needs_flush flag when batch reaches 200-byte limit.
 *
 * @param batch Pointer to batch structure
 * @param scancode HID scancode
 * @param modifier Modifier flags (Shift=0x02, Ctrl=0x01, etc.)
 * @param character ASCII character
 * @param timestamp_us Microsecond timestamp
 * @return true if keystroke added successfully, false if batch full
 */
bool meshstatic_batch_add(meshstatic_batch_t* batch,
                          uint8_t scancode,
                          uint8_t modifier,
                          char character,
                          uint32_t timestamp_us);

/**
 * @brief Check if batch is full and needs flushing
 *
 * Returns true when batch reaches 200-byte limit or max keystroke count.
 *
 * @param batch Pointer to batch structure
 * @return true if batch should be flushed to storage
 */
bool meshstatic_batch_is_full(const meshstatic_batch_t* batch);

/**
 * @brief Get CSV string representation of batch
 *
 * Returns pointer to internal CSV buffer (null-terminated string).
 * CSV format includes header + all keystroke rows.
 *
 * @param batch Pointer to batch structure
 * @return Pointer to CSV string (read-only)
 */
const char* meshstatic_batch_get_csv(const meshstatic_batch_t* batch);

/**
 * @brief Get CSV string length (bytes)
 *
 * @param batch Pointer to batch structure
 * @return CSV string length including null terminator
 */
uint32_t meshstatic_batch_get_csv_length(const meshstatic_batch_t* batch);

/**
 * @brief Reset batch after flushing
 *
 * Clears all keystrokes, resets metadata, and reinitializes CSV buffer.
 * Batch ID is incremented automatically.
 *
 * @param batch Pointer to batch structure
 */
void meshstatic_batch_reset(meshstatic_batch_t* batch);

/**
 * @brief Get batch statistics
 *
 * @param batch Pointer to batch structure
 * @param count Output: number of keystrokes in batch
 * @param csv_length Output: current CSV length in bytes
 * @param batch_id Output: current batch ID
 */
void meshstatic_batch_get_stats(const meshstatic_batch_t* batch,
                                uint32_t* count,
                                uint32_t* csv_length,
                                uint32_t* batch_id);

#ifdef __cplusplus
}
#endif

#endif /* MESHSTATIC_BATCH_H */
