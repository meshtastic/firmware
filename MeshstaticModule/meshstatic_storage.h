/**
 * @file meshstatic_storage.h
 * @brief Flash storage manager for CSV batch files
 *
 * Purpose: Manage CSV batch files on flash filesystem with LittleFS.
 * Handles file creation, deletion, listing, and batch metadata tracking.
 *
 * Design Goals:
 * - Simple file-based storage (no database complexity)
 * - Robust error handling for flash operations
 * - Automatic cleanup of old batches
 * - Power-loss recovery support
 *
 * File Naming Convention:
 *   batch_00001.csv
 *   batch_00002.csv
 *   batch_XXXXX.csv  (5-digit zero-padded batch ID)
 *
 * Storage Layout:
 *   /meshstatic/
 *   ├── batch_00001.csv  (200 bytes)
 *   ├── batch_00002.csv  (200 bytes)
 *   └── metadata.txt     (batch index)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MESHSTATIC_STORAGE_H
#define MESHSTATIC_STORAGE_H

#include "meshstatic_batch.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants and Configuration
 * ============================================================================ */

/** Storage directory for meshstatic batches */
#define MESHSTATIC_STORAGE_DIR "/meshstatic"

/** Batch file prefix */
#define MESHSTATIC_FILE_PREFIX "batch_"

/** Batch file extension */
#define MESHSTATIC_FILE_EXT ".csv"

/** Maximum filename length: "batch_00001.csv" = 16 bytes */
#define MESHSTATIC_MAX_FILENAME 32

/** Maximum number of batch files to keep */
#define MESHSTATIC_MAX_BATCH_FILES 100

/** Metadata file name */
#define MESHSTATIC_METADATA_FILE "metadata.txt"

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/**
 * @brief Storage statistics
 */
typedef struct {
    uint32_t total_batches;         /**< Total number of batch files */
    uint32_t total_bytes;           /**< Total storage used (bytes) */
    uint32_t oldest_batch_id;       /**< Oldest batch ID in storage */
    uint32_t newest_batch_id;       /**< Newest batch ID in storage */
    bool storage_full;              /**< True if max files reached */
} meshstatic_storage_stats_t;

/**
 * @brief Storage initialization result
 */
typedef struct {
    bool success;                   /**< True if initialization successful */
    uint32_t recovered_batches;     /**< Number of batches recovered from flash */
    char error_msg[64];             /**< Error message if failed */
} meshstatic_storage_init_result_t;

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * @brief Initialize storage system
 *
 * Creates storage directory if needed and recovers existing batches.
 * This is a POSIX implementation using standard FILE* operations.
 * For RP2350 integration, this will be adapted to use LittleFS API.
 *
 * @return Initialization result (success, recovered batches, error message)
 */
meshstatic_storage_init_result_t meshstatic_storage_init(void);

/**
 * @brief Save batch to flash as CSV file
 *
 * Writes batch CSV to file: /meshstatic/batch_XXXXX.csv
 * File size is always ≤ 200 bytes (enforced by batch manager).
 *
 * @param batch Pointer to batch structure
 * @return true if saved successfully, false on error
 */
bool meshstatic_storage_save_batch(const meshstatic_batch_t* batch);

/**
 * @brief Load batch from flash by ID
 *
 * Reads CSV file and reconstructs batch structure.
 * Note: This parses CSV back into keystroke array.
 *
 * @param batch_id Batch ID to load
 * @param batch Output: reconstructed batch structure
 * @return true if loaded successfully, false if not found or error
 */
bool meshstatic_storage_load_batch(uint32_t batch_id, meshstatic_batch_t* batch);

/**
 * @brief Delete batch file by ID
 *
 * Removes CSV file from storage.
 * Use this after successful transmission to free space.
 *
 * @param batch_id Batch ID to delete
 * @return true if deleted successfully, false on error
 */
bool meshstatic_storage_delete_batch(uint32_t batch_id);

/**
 * @brief Check if batch file exists
 *
 * @param batch_id Batch ID to check
 * @return true if file exists in storage
 */
bool meshstatic_storage_batch_exists(uint32_t batch_id);

/**
 * @brief Get list of all batch IDs in storage
 *
 * Returns array of batch IDs sorted in ascending order.
 * Caller must free returned array when done.
 *
 * @param count Output: number of batches
 * @return Dynamically allocated array of batch IDs (or NULL on error)
 */
uint32_t* meshstatic_storage_list_batches(uint32_t* count);

/**
 * @brief Get storage statistics
 *
 * @param stats Output: storage statistics
 */
void meshstatic_storage_get_stats(meshstatic_storage_stats_t* stats);

/**
 * @brief Delete oldest batches to make space
 *
 * Removes oldest N batches from storage.
 * Use this when storage full or for periodic cleanup.
 *
 * @param count Number of oldest batches to delete
 * @return Number of batches actually deleted
 */
uint32_t meshstatic_storage_cleanup_old(uint32_t count);

/**
 * @brief Export batch to string (for transmission)
 *
 * Reads CSV file and returns contents as string.
 * Caller must free returned string when done.
 *
 * @param batch_id Batch ID to export
 * @param length Output: CSV string length
 * @return Dynamically allocated CSV string (or NULL on error)
 */
char* meshstatic_storage_export_batch(uint32_t batch_id, uint32_t* length);

/**
 * @brief Get next batch ID for transmission
 *
 * Returns the oldest batch ID that hasn't been transmitted yet.
 * Use this to implement sequential transmission queue.
 *
 * @return Batch ID to transmit next, or 0 if none available
 */
uint32_t meshstatic_storage_get_next_to_transmit(void);

/**
 * @brief Mark batch as transmitted (for future deletion)
 *
 * Updates metadata to track transmission status.
 * Use this before calling delete_batch().
 *
 * @param batch_id Batch ID that was transmitted
 */
void meshstatic_storage_mark_transmitted(uint32_t batch_id);

/* ============================================================================
 * Internal Helper Functions (exposed for testing)
 * ============================================================================ */

/**
 * @brief Generate filename from batch ID
 *
 * Format: "batch_00001.csv"
 *
 * @param batch_id Batch ID
 * @param filename Output: filename buffer (min 32 bytes)
 */
void meshstatic_storage_format_filename(uint32_t batch_id, char* filename);

/**
 * @brief Get full path to batch file
 *
 * Format: "/meshstatic/batch_00001.csv"
 *
 * @param batch_id Batch ID
 * @param path Output: path buffer (min 64 bytes)
 */
void meshstatic_storage_get_full_path(uint32_t batch_id, char* path);

#ifdef __cplusplus
}
#endif

#endif /* MESHSTATIC_STORAGE_H */
