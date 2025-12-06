/**
 * @file meshstatic_core1.h
 * @brief Core 1 controller for meshstatic module integration
 *
 * Purpose: Main integration point for RP2350 Core 1 USB capture loop.
 * Coordinates Components 1 & 2 to provide complete keystroke capture,
 * batching, and flash storage functionality.
 *
 * Integration Flow:
 *   USB Capture → meshstatic_core1_add_keystroke() →
 *   Batch Manager (Component 1) → Storage Manager (Component 2) →
 *   Flash Files (.csv)
 *
 * Usage in capture_v2.cpp:
 *   ```c
 *   #include "meshstatic_core1.h"
 *
 *   // In capture_controller_core1_main_v2():
 *   meshstatic_core1_init();
 *
 *   // After keystroke decode:
 *   meshstatic_core1_add_keystroke(scancode, modifier, character, timestamp_us);
 *   ```
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MESHSTATIC_CORE1_H
#define MESHSTATIC_CORE1_H

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

/** Auto-flush batch every N seconds if not full */
#define MESHSTATIC_AUTO_FLUSH_TIMEOUT_US (10 * 1000000)  /* 10 seconds */

/** Enable debug logging (set to 0 for production) */
#define MESHSTATIC_DEBUG_ENABLED 1

/* ============================================================================
 * Statistics
 * ============================================================================ */

/**
 * @brief Core 1 controller statistics
 */
typedef struct {
    uint32_t keystrokes_captured;   /**< Total keystrokes added to batch */
    uint32_t batches_created;       /**< Total batches created */
    uint32_t batches_saved;         /**< Total batches saved to flash */
    uint32_t save_errors;           /**< Number of save failures */
    uint32_t auto_flushes;          /**< Number of automatic timeout flushes */
    uint32_t manual_flushes;        /**< Number of manual flush_batch() calls */
    uint32_t current_batch_count;   /**< Keystrokes in current batch */
    uint32_t current_batch_id;      /**< Current batch ID */
    uint64_t last_keystroke_us;     /**< Timestamp of last keystroke */
} meshstatic_core1_stats_t;

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * @brief Initialize meshstatic module on Core 1
 *
 * Call this once at Core 1 startup, after USB capture initialization.
 * Initializes storage system and creates first batch.
 *
 * @return true if initialization successful, false on error
 */
bool meshstatic_core1_init(void);

/**
 * @brief Add captured keystroke to batch
 *
 * This is the main integration point - call this from USB capture loop
 * after successful keystroke decode.
 *
 * Behavior:
 * - Adds keystroke to current batch
 * - If batch full (200-byte limit), automatically flushes to flash
 * - Creates new batch after flush
 *
 * @param scancode HID scancode
 * @param modifier Modifier flags (Shift, Ctrl, Alt, etc.)
 * @param character ASCII character
 * @param timestamp_us Microsecond timestamp
 * @return true if keystroke added successfully
 */
bool meshstatic_core1_add_keystroke(uint8_t scancode,
                                    uint8_t modifier,
                                    char character,
                                    uint32_t timestamp_us);

/**
 * @brief Manually flush current batch to flash
 *
 * Saves current batch to flash even if not full.
 * Use this for:
 * - Periodic timeout flushes
 * - Before power-down
 * - After significant idle period
 *
 * @return true if flush successful, false if batch empty or save error
 */
bool meshstatic_core1_flush_batch(void);

/**
 * @brief Check if automatic flush needed (timeout-based)
 *
 * Call this periodically in Core 1 loop to implement auto-flush.
 * Flushes batch if:
 * - Batch not empty AND
 * - Time since last keystroke > MESHSTATIC_AUTO_FLUSH_TIMEOUT_US
 *
 * @param current_time_us Current microsecond timestamp
 * @return true if auto-flush was triggered
 */
bool meshstatic_core1_check_auto_flush(uint64_t current_time_us);

/**
 * @brief Get current batch statistics
 *
 * @param stats Output: controller statistics
 */
void meshstatic_core1_get_stats(meshstatic_core1_stats_t* stats);

/**
 * @brief Reset statistics (for testing/debugging)
 */
void meshstatic_core1_reset_stats(void);

/**
 * @brief Shutdown meshstatic module
 *
 * Flushes current batch and cleans up resources.
 * Call this before Core 1 shutdown.
 */
void meshstatic_core1_shutdown(void);

/**
 * @brief Get pointer to current batch (for inspection)
 *
 * @return Pointer to current batch (read-only)
 */
const meshstatic_batch_t* meshstatic_core1_get_current_batch(void);

/* ============================================================================
 * Debug/Testing Helpers
 * ============================================================================ */

#if MESHSTATIC_DEBUG_ENABLED

/**
 * @brief Print current controller statistics to stdout
 */
void meshstatic_core1_print_stats(void);

/**
 * @brief Print current batch info to stdout
 */
void meshstatic_core1_print_batch_info(void);

#endif /* MESHSTATIC_DEBUG_ENABLED */

#ifdef __cplusplus
}
#endif

#endif /* MESHSTATIC_CORE1_H */
