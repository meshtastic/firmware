/**
 * @file CSVBatchModule.h
 * @brief Independent CSV batch consumer module
 *
 * Purpose: Consume keystrokes from USBCaptureModule queue and create
 * CSV batch files with 200-byte limit.
 *
 * Design Goals:
 * - Single responsibility: Read queue → Batch → Save CSV
 * - NO USB capture (consumes from USBCaptureModule queue)
 * - Can run on Core 0 or Core 1 (your choice)
 * - Independent from USB capture timing
 *
 * Architecture:
 *   USBCaptureModule (Core 1) → Lock-Free Queue → CSVBatchModule (Core 0/1)
 *
 * Integration:
 *   // Initialization (Core 0 or Core 1):
 *   csv_batch_module_init();
 *
 *   // Periodic processing (Core 0 or Core 1):
 *   csv_batch_module_process();  // Reads queue, batches, saves
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CSV_BATCH_MODULE_H
#define CSV_BATCH_MODULE_H

#include "USBCaptureModule.h"
#include "meshstatic_batch.h"
#include "meshstatic_storage.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration
 * ============================================================================ */

/** Auto-flush timeout (microseconds) - 10 seconds */
#define CSV_BATCH_AUTO_FLUSH_TIMEOUT_US (10 * 1000000ULL)

/** Maximum events to process per iteration */
#define CSV_BATCH_MAX_PROCESS_PER_LOOP 16

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/**
 * @brief CSV batch module statistics
 */
typedef struct {
    uint32_t events_consumed;       /**< Total events read from queue */
    uint32_t batches_created;       /**< Total batches created */
    uint32_t batches_saved;         /**< Total batches saved to flash */
    uint32_t save_errors;           /**< Number of save failures */
    uint32_t auto_flushes;          /**< Number of timeout flushes */
    uint32_t manual_flushes;        /**< Number of manual flushes */
    uint32_t current_batch_count;   /**< Keystrokes in current batch */
    uint32_t current_batch_id;      /**< Current batch ID */
    uint64_t last_event_time_us;    /**< Timestamp of last event */
} csv_batch_stats_t;

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * @brief Initialize CSV batch module
 *
 * Call this once during module startup (Core 0 or Core 1).
 *
 * @return true if initialization successful
 */
bool csv_batch_module_init(void);

/**
 * @brief Process keystroke events from USBCaptureModule queue
 *
 * Call this periodically to:
 * - Dequeue events from USBCaptureModule
 * - Add to current CSV batch
 * - Save batch when full (200-byte limit)
 * - Auto-flush on timeout (10 seconds idle)
 *
 * This function is non-blocking and can be called from Core 0 or Core 1.
 *
 * @return Number of events processed this iteration
 */
uint32_t csv_batch_module_process(void);

/**
 * @brief Manually flush current batch to flash
 *
 * Forces immediate save of current batch (even if not full).
 * Use this for:
 * - Periodic timeout flushes
 * - Before shutdown
 * - After significant idle period
 *
 * @return true if flush successful, false if batch empty
 */
bool csv_batch_module_flush(void);

/**
 * @brief Check if auto-flush needed (timeout-based)
 *
 * Call this periodically to implement automatic timeout flushing.
 *
 * @param current_time_us Current microsecond timestamp
 * @return true if auto-flush was triggered
 */
bool csv_batch_module_check_auto_flush(uint64_t current_time_us);

/**
 * @brief Get module statistics
 *
 * Thread-safe for multi-core access.
 *
 * @param stats Output: module statistics
 */
void csv_batch_module_get_stats(csv_batch_stats_t* stats);

/**
 * @brief Reset statistics
 */
void csv_batch_module_reset_stats(void);

/**
 * @brief Print module statistics to stdout
 */
void csv_batch_module_print_stats(void);

/**
 * @brief Shutdown module
 *
 * Flushes remaining batch and cleans up resources.
 */
void csv_batch_module_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* CSV_BATCH_MODULE_H */
