/**
 * @file psram_buffer.cpp
 * @brief Legacy buffer struct definitions and statistics tracking (v7.0)
 *
 * NOTE: The 8-slot ring buffer has been replaced by MinimalBatchBuffer (2 slots)
 * in v7.0. This file now only provides:
 *  - psram_keystroke_buffer_t struct (used for batch unpacking)
 *  - g_psram_buffer.header (used for statistics tracking)
 *
 * The ring buffer functions (write, read, has_data, get_count) are no longer
 * used and have been removed. Use MinimalBatchBuffer for fallback storage.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "psram_buffer.h"
#include <string.h>

/**
 * Global buffer instance - used for:
 *  - psram_keystroke_buffer_t struct definition (batch format)
 *  - g_psram_buffer.header.* statistics tracking
 *
 * NOTE: The ring buffer slots are no longer used. MinimalBatchBuffer
 * provides the 2-slot RAM fallback instead.
 */
psram_buffer_t g_psram_buffer;

/**
 * @brief Initialize statistics counters
 *
 * Zeros all statistics counters. Called during module initialization.
 * Note: As a global variable, g_psram_buffer is already zero-initialized
 * by default, but this provides explicit initialization if needed.
 */
void psram_buffer_init() {
    // Zero out entire structure
    memset(&g_psram_buffer, 0, sizeof(psram_buffer_t));

    // Set magic number for validation
    g_psram_buffer.header.magic = PSRAM_MAGIC;

    // Statistics start at zero (already zeroed by memset)
}

/*
 * NOTE: Ring buffer functions removed in v7.0
 *
 * The following functions were part of the 8-slot ring buffer and are no
 * longer used. MinimalBatchBuffer (2 slots) provides the RAM fallback:
 *
 * - psram_buffer_write()     -> minimal_buffer_write()
 * - psram_buffer_read()      -> minimal_buffer_read()
 * - psram_buffer_has_data()  -> minimal_buffer_has_data()
 * - psram_buffer_get_count() -> minimal_buffer_count()
 * - psram_buffer_dump()      -> inline LOG_INFO in CMD_DUMP handler
 */
