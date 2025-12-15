/**
 * @file MinimalBatchBuffer.h
 * @brief Minimal 2-slot RAM buffer for FRAM fallback (v7.0)
 *
 * A lightweight RAM buffer used only when FRAM storage is unavailable.
 * Implements a simple 2-slot ring buffer for graceful degradation.
 *
 * NASA Power of 10 Compliance:
 *  - Rule 1: No recursion
 *  - Rule 2: All loops bounded (max 2 iterations)
 *  - Rule 3: No dynamic memory after init (static allocation)
 *  - Rule 4: No function longer than 60 lines
 *  - Rule 5: Two assertions per function minimum
 *  - Rule 6: Data declarations at smallest scope
 *  - Rule 7: Check return values
 *  - Rule 8: Limited preprocessor use
 *  - Rule 9: Limited pointer use
 *  - Rule 10: Compile with all warnings, static analysis
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MINIMAL_BATCH_BUFFER_H
#define MINIMAL_BATCH_BUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Buffer configuration constants
 */
#define MINIMAL_BUFFER_SLOTS      2       /**< Only 2 batches (minimal fallback) */
#define MINIMAL_BUFFER_DATA_SIZE  512     /**< Max batch size in bytes */
#define MINIMAL_BUFFER_MAGIC      0xBABE  /**< Validation magic number */

/**
 * @brief Individual batch slot
 */
typedef struct {
    uint32_t batch_id;                      /**< Batch ID for ACK correlation */
    uint16_t data_length;                   /**< Actual data length (0 = empty) */
    uint16_t flags;                         /**< Reserved for future use */
    uint8_t data[MINIMAL_BUFFER_DATA_SIZE]; /**< Batch data */
} minimal_batch_slot_t;

/**
 * @brief Buffer header for ring management
 */
typedef struct {
    uint16_t magic;                /**< Validation magic */
    uint8_t write_index;           /**< Next write position (0-1) */
    uint8_t read_index;            /**< Next read position (0-1) */
    uint8_t batch_count;           /**< Number of batches stored (0-2) */
    uint8_t reserved[3];           /**< Padding to 8 bytes */
} minimal_buffer_header_t;

/**
 * @brief Complete buffer structure
 * Header (8 bytes) + 2 slots (2 * 520 bytes) = 1048 bytes total
 */
typedef struct {
    minimal_buffer_header_t header;
    minimal_batch_slot_t slots[MINIMAL_BUFFER_SLOTS];
} minimal_batch_buffer_t;

/**
 * @brief Initialize the minimal buffer
 *
 * Clears all slots and initializes header.
 * Must be called once before first use.
 *
 * NASA Rule 5: Contains 2+ assertions
 */
void minimal_buffer_init(void);

/**
 * @brief Write a batch to the buffer
 *
 * @param data Pointer to batch data
 * @param length Length of batch data (must be <= MINIMAL_BUFFER_DATA_SIZE)
 * @param batch_id Batch ID for ACK correlation
 * @return true if written, false if buffer full (2 batches already stored)
 *
 * NASA Rule 5: Contains 2+ assertions
 * NASA Rule 7: Returns meaningful value
 */
bool minimal_buffer_write(const uint8_t *data, uint16_t length, uint32_t batch_id);

/**
 * @brief Read (peek) the oldest batch without removing
 *
 * @param buffer Output buffer to receive data
 * @param max_length Maximum bytes to read
 * @param actual_length Output: actual bytes read
 * @param batch_id Output: batch ID of the data
 * @return true if data available, false if buffer empty
 *
 * NASA Rule 5: Contains 2+ assertions
 * NASA Rule 7: Returns meaningful value
 */
bool minimal_buffer_read(uint8_t *buffer, uint16_t max_length,
                         uint16_t *actual_length, uint32_t *batch_id);

/**
 * @brief Delete the oldest batch (after successful transmission)
 *
 * Call this after receiving ACK for the batch.
 *
 * @return true if deleted, false if buffer was empty
 *
 * NASA Rule 5: Contains 2+ assertions
 */
bool minimal_buffer_delete(void);

/**
 * @brief Check if buffer has any data
 *
 * @return true if one or more batches available
 *
 * NASA Rule 5: Contains 2+ assertions
 */
bool minimal_buffer_has_data(void);

/**
 * @brief Get number of batches in buffer
 *
 * @return Number of batches (0, 1, or 2)
 *
 * NASA Rule 5: Contains 2+ assertions
 */
uint8_t minimal_buffer_count(void);

#ifdef __cplusplus
}
#endif

#endif /* MINIMAL_BATCH_BUFFER_H */
