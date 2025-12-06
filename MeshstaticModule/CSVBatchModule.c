/**
 * @file CSVBatchModule.c
 * @brief Implementation of CSV batch consumer module
 *
 * Consumes keystrokes from USBCaptureModule queue and creates CSV batches.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "CSVBatchModule.h"
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Private State
 * ============================================================================ */

/** Current active batch */
static meshstatic_batch_t g_current_batch;

/** Module statistics */
static csv_batch_stats_t g_stats;

/** Initialization flag */
static bool g_initialized = false;

/* ============================================================================
 * Private Helper Functions
 * ============================================================================ */

/**
 * @brief Flush current batch to storage
 *
 * @return true if flush successful
 */
static bool flush_batch(void)
{
    /* Don't flush empty batches */
    if (g_current_batch.meta.count == 0) {
        return false;
    }

    /* Save to storage */
    bool success = meshstatic_storage_save_batch(&g_current_batch);

    if (success) {
        g_stats.batches_saved++;

        printf("[CSV_BATCH] Batch %u saved: %u keystrokes, %u bytes\n",
               g_current_batch.meta.batch_id,
               g_current_batch.meta.count,
               g_current_batch.meta.csv_length);
    } else {
        g_stats.save_errors++;

        printf("[CSV_BATCH] ERROR: Failed to save batch %u\n",
               g_current_batch.meta.batch_id);
    }

    /* Reset batch */
    meshstatic_batch_reset(&g_current_batch);
    g_stats.batches_created++;

    return success;
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

bool csv_batch_module_init(void)
{
    if (g_initialized) {
        return true;
    }

    /* Initialize storage */
    meshstatic_storage_init_result_t storage_result = meshstatic_storage_init();

    if (!storage_result.success) {
        printf("[CSV_BATCH] ERROR: Storage init failed: %s\n",
               storage_result.error_msg);
        return false;
    }

    printf("[CSV_BATCH] Storage initialized (recovered %u batches)\n",
           storage_result.recovered_batches);

    /* Initialize first batch */
    meshstatic_batch_init(&g_current_batch);

    /* Reset statistics */
    memset(&g_stats, 0, sizeof(csv_batch_stats_t));
    g_stats.batches_created = 1;
    g_stats.current_batch_id = g_current_batch.meta.batch_id;

    g_initialized = true;

    printf("[CSV_BATCH] Module initialized (batch ID: %u)\n",
           g_current_batch.meta.batch_id);

    return true;
}

uint32_t csv_batch_module_process(void)
{
    if (!g_initialized) {
        return 0;
    }

    uint32_t processed = 0;

    /* Process up to MAX_PROCESS_PER_LOOP events per iteration */
    for (uint32_t i = 0; i < CSV_BATCH_MAX_PROCESS_PER_LOOP; i++) {
        /* Check if events available in USBCaptureModule queue */
        if (!usb_capture_module_available()) {
            break;  /* Queue empty */
        }

        /* Pop event from queue */
        keystroke_event_t event;
        if (!usb_capture_module_pop(&event)) {
            break;  /* Failed to pop */
        }

        /* Add to current batch */
        bool added = meshstatic_batch_add(&g_current_batch,
                                          event.scancode,
                                          event.modifier,
                                          event.character,
                                          event.timestamp_us);

        if (added) {
            g_stats.events_consumed++;
            g_stats.last_event_time_us = event.timestamp_us;
            processed++;

            /* Check if batch full */
            if (meshstatic_batch_is_full(&g_current_batch)) {
                flush_batch();
            }
        } else {
            /* Batch full but wasn't caught - force flush */
            flush_batch();

            /* Retry with new batch */
            meshstatic_batch_add(&g_current_batch,
                                event.scancode,
                                event.modifier,
                                event.character,
                                event.timestamp_us);
        }
    }

    /* Update current batch stats */
    g_stats.current_batch_count = g_current_batch.meta.count;
    g_stats.current_batch_id = g_current_batch.meta.batch_id;

    return processed;
}

bool csv_batch_module_flush(void)
{
    if (!g_initialized) {
        return false;
    }

    g_stats.manual_flushes++;

    return flush_batch();
}

bool csv_batch_module_check_auto_flush(uint64_t current_time_us)
{
    if (!g_initialized) {
        return false;
    }

    /* Don't flush empty batches */
    if (g_current_batch.meta.count == 0) {
        return false;
    }

    /* Check timeout */
    uint64_t time_since_last = current_time_us - g_stats.last_event_time_us;

    if (time_since_last >= CSV_BATCH_AUTO_FLUSH_TIMEOUT_US) {
        printf("[CSV_BATCH] Auto-flush timeout (%llu seconds idle)\n",
               time_since_last / 1000000);

        g_stats.auto_flushes++;
        return flush_batch();
    }

    return false;
}

void csv_batch_module_get_stats(csv_batch_stats_t* stats)
{
    if (!stats) {
        return;
    }

    memcpy(stats, &g_stats, sizeof(csv_batch_stats_t));

    /* Update current batch info */
    stats->current_batch_count = g_current_batch.meta.count;
    stats->current_batch_id = g_current_batch.meta.batch_id;
}

void csv_batch_module_reset_stats(void)
{
    memset(&g_stats, 0, sizeof(csv_batch_stats_t));

    /* Preserve current batch info */
    g_stats.current_batch_count = g_current_batch.meta.count;
    g_stats.current_batch_id = g_current_batch.meta.batch_id;
    g_stats.batches_created = 1;
}

void csv_batch_module_print_stats(void)
{
    csv_batch_stats_t stats;
    csv_batch_module_get_stats(&stats);

    printf("========== CSV BATCH MODULE STATISTICS ==========\n");
    printf("Events Consumed:    %u\n", stats.events_consumed);
    printf("Batches Created:    %u\n", stats.batches_created);
    printf("Batches Saved:      %u\n", stats.batches_saved);
    printf("Save Errors:        %u\n", stats.save_errors);
    printf("Auto Flushes:       %u\n", stats.auto_flushes);
    printf("Manual Flushes:     %u\n", stats.manual_flushes);
    printf("Current Batch ID:   %u\n", stats.current_batch_id);
    printf("Current Batch Count:%u/%u\n",
           stats.current_batch_count,
           MESHSTATIC_MAX_KEYSTROKES_PER_BATCH);
    printf("Last Event Time:    %llu us\n",
           (unsigned long long)stats.last_event_time_us);
    printf("==================================================\n");
}

void csv_batch_module_shutdown(void)
{
    if (!g_initialized) {
        return;
    }

    printf("[CSV_BATCH] Shutting down - flushing final batch\n");

    /* Flush remaining keystrokes */
    flush_batch();

    g_initialized = false;
}
