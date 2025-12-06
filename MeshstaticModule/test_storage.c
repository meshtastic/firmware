/**
 * @file test_storage.c
 * @brief Test program for meshstatic storage manager
 *
 * Tests Component 2 integration with Component 1:
 * - Initialize storage system
 * - Save batches to flash (CSV files)
 * - Load batches from flash
 * - Delete batches
 * - List batches
 * - Storage statistics
 *
 * Compile:
 *   gcc test_storage.c meshstatic_storage.c meshstatic_batch.c -o test_storage
 *
 * Run:
 *   ./test_storage
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshstatic_storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * Test Utilities
 * ============================================================================ */

/**
 * @brief Print storage statistics
 */
void print_storage_stats(void)
{
    meshstatic_storage_stats_t stats;
    meshstatic_storage_get_stats(&stats);

    printf("========================================\n");
    printf("Storage Statistics:\n");
    printf("  Total Batches:  %u\n", stats.total_batches);
    printf("  Total Bytes:    %u\n", stats.total_bytes);
    printf("  Oldest Batch:   %u\n", stats.oldest_batch_id);
    printf("  Newest Batch:   %u\n", stats.newest_batch_id);
    printf("  Storage Full:   %s\n", stats.storage_full ? "YES" : "NO");
    printf("========================================\n");
}

/**
 * @brief Get current microsecond timestamp (simulated)
 */
uint32_t get_timestamp_us(void)
{
    static uint32_t base_time = 0;
    if (base_time == 0) {
        base_time = (uint32_t)(clock() * 1000000 / CLOCKS_PER_SEC);
    }
    return (uint32_t)(clock() * 1000000 / CLOCKS_PER_SEC) - base_time;
}

/* ============================================================================
 * Test Cases
 * ============================================================================ */

/**
 * @brief Test 1: Storage initialization
 */
void test_storage_init(void)
{
    printf("\n=== Test 1: Storage Initialization ===\n");

    meshstatic_storage_init_result_t result = meshstatic_storage_init();

    if (result.success) {
        printf("✓ Storage initialized successfully\n");
        printf("  Recovered batches: %u\n", result.recovered_batches);
    } else {
        printf("✗ Storage initialization failed: %s\n", result.error_msg);
    }

    print_storage_stats();
}

/**
 * @brief Test 2: Save batch to storage
 */
void test_save_batch(void)
{
    printf("\n=== Test 2: Save Batch to Storage ===\n");

    /* Create a batch */
    meshstatic_batch_t batch;
    meshstatic_batch_init(&batch);

    /* Add some keystrokes */
    meshstatic_batch_add(&batch, 0x04, 0x00, 'a', get_timestamp_us());
    meshstatic_batch_add(&batch, 0x05, 0x00, 'b', get_timestamp_us());
    meshstatic_batch_add(&batch, 0x06, 0x00, 'c', get_timestamp_us());

    printf("Batch created: ID=%u, CSV length=%u bytes\n",
           batch.meta.batch_id, batch.meta.csv_length);

    /* Save to storage */
    if (meshstatic_storage_save_batch(&batch)) {
        printf("✓ Batch saved to storage: batch_%05u.csv\n", batch.meta.batch_id);

        /* Verify file exists */
        if (meshstatic_storage_batch_exists(batch.meta.batch_id)) {
            printf("✓ Batch file verified to exist\n");
        } else {
            printf("✗ Batch file not found after save!\n");
        }
    } else {
        printf("✗ Failed to save batch\n");
    }

    print_storage_stats();
}

/**
 * @brief Test 3: Load batch from storage
 */
void test_load_batch(void)
{
    printf("\n=== Test 3: Load Batch from Storage ===\n");

    /* First, save a batch */
    meshstatic_batch_t original;
    meshstatic_batch_init(&original);
    meshstatic_batch_add(&original, 0x04, 0x00, 'X', get_timestamp_us());
    meshstatic_batch_add(&original, 0x05, 0x00, 'Y', get_timestamp_us());

    uint32_t batch_id = original.meta.batch_id;
    meshstatic_storage_save_batch(&original);

    printf("Saved original batch: ID=%u, CSV length=%u bytes\n",
           batch_id, original.meta.csv_length);

    /* Now load it back */
    meshstatic_batch_t loaded;
    if (meshstatic_storage_load_batch(batch_id, &loaded)) {
        printf("✓ Batch loaded from storage\n");
        printf("  Loaded batch ID:   %u\n", loaded.meta.batch_id);
        printf("  Loaded CSV length: %u bytes\n", loaded.meta.csv_length);

        /* Verify CSV content */
        if (strcmp(loaded.csv_buffer, original.csv_buffer) == 0) {
            printf("✓ CSV content matches original\n");
        } else {
            printf("✗ CSV content mismatch!\n");
        }
    } else {
        printf("✗ Failed to load batch\n");
    }
}

/**
 * @brief Test 4: List all batches
 */
void test_list_batches(void)
{
    printf("\n=== Test 4: List All Batches ===\n");

    uint32_t count = 0;
    uint32_t* batch_ids = meshstatic_storage_list_batches(&count);

    if (batch_ids && count > 0) {
        printf("✓ Found %u batches in storage:\n", count);
        for (uint32_t i = 0; i < count; i++) {
            printf("  Batch %u: ID=%u (batch_%05u.csv)\n",
                   i + 1, batch_ids[i], batch_ids[i]);
        }
        free(batch_ids);
    } else {
        printf("  No batches found in storage\n");
    }
}

/**
 * @brief Test 5: Delete batch
 */
void test_delete_batch(void)
{
    printf("\n=== Test 5: Delete Batch ===\n");

    /* Create and save a batch */
    meshstatic_batch_t batch;
    meshstatic_batch_init(&batch);
    meshstatic_batch_add(&batch, 0x04, 0x00, 'D', get_timestamp_us());

    uint32_t batch_id = batch.meta.batch_id;
    meshstatic_storage_save_batch(&batch);

    printf("Created batch to delete: ID=%u\n", batch_id);

    /* Verify it exists */
    if (meshstatic_storage_batch_exists(batch_id)) {
        printf("✓ Batch exists before deletion\n");

        /* Delete it */
        if (meshstatic_storage_delete_batch(batch_id)) {
            printf("✓ Batch deleted successfully\n");

            /* Verify it's gone */
            if (!meshstatic_storage_batch_exists(batch_id)) {
                printf("✓ Batch no longer exists\n");
            } else {
                printf("✗ Batch still exists after deletion!\n");
            }
        } else {
            printf("✗ Failed to delete batch\n");
        }
    }
}

/**
 * @brief Test 6: Export batch for transmission
 */
void test_export_batch(void)
{
    printf("\n=== Test 6: Export Batch for Transmission ===\n");

    /* Create and save a batch */
    meshstatic_batch_t batch;
    meshstatic_batch_init(&batch);
    meshstatic_batch_add(&batch, 0x04, 0x00, 'E', get_timestamp_us());
    meshstatic_batch_add(&batch, 0x05, 0x00, 'F', get_timestamp_us());

    uint32_t batch_id = batch.meta.batch_id;
    meshstatic_storage_save_batch(&batch);

    printf("Batch saved: ID=%u\n", batch_id);

    /* Export for transmission */
    uint32_t length = 0;
    char* csv = meshstatic_storage_export_batch(batch_id, &length);

    if (csv) {
        printf("✓ Batch exported successfully\n");
        printf("  Export length: %u bytes\n", length);
        printf("\n--- Exported CSV ---\n");
        printf("%s", csv);
        printf("--- End Export ---\n\n");

        free(csv);
    } else {
        printf("✗ Failed to export batch\n");
    }
}

/**
 * @brief Test 7: Multiple batch workflow
 */
void test_multiple_batches(void)
{
    printf("\n=== Test 7: Multiple Batch Workflow ===\n");

    /* Create and save 5 batches */
    const int num_batches = 5;
    printf("Creating and saving %d batches...\n", num_batches);

    for (int i = 0; i < num_batches; i++) {
        meshstatic_batch_t batch;
        meshstatic_batch_init(&batch);

        /* Add some keystrokes */
        char c = 'A' + i;
        meshstatic_batch_add(&batch, 0x04 + i, 0x00, c, get_timestamp_us());
        meshstatic_batch_add(&batch, 0x04 + i, 0x00, c + 1, get_timestamp_us());

        if (meshstatic_storage_save_batch(&batch)) {
            printf("  ✓ Saved batch %u\n", batch.meta.batch_id);
        } else {
            printf("  ✗ Failed to save batch %u\n", batch.meta.batch_id);
        }
    }

    print_storage_stats();
    test_list_batches();
}

/**
 * @brief Test 8: Cleanup old batches
 */
void test_cleanup_old(void)
{
    printf("\n=== Test 8: Cleanup Old Batches ===\n");

    /* Get current stats */
    meshstatic_storage_stats_t stats;
    meshstatic_storage_get_stats(&stats);

    printf("Before cleanup: %u batches\n", stats.total_batches);

    /* Delete oldest 2 batches */
    uint32_t deleted = meshstatic_storage_cleanup_old(2);

    printf("✓ Deleted %u old batches\n", deleted);

    /* Get new stats */
    meshstatic_storage_get_stats(&stats);
    printf("After cleanup: %u batches\n", stats.total_batches);

    print_storage_stats();
}

/**
 * @brief Test 9: Get next batch to transmit
 */
void test_next_to_transmit(void)
{
    printf("\n=== Test 9: Get Next Batch to Transmit ===\n");

    uint32_t next_id = meshstatic_storage_get_next_to_transmit();

    if (next_id > 0) {
        printf("✓ Next batch to transmit: ID=%u\n", next_id);

        /* Export it */
        uint32_t length = 0;
        char* csv = meshstatic_storage_export_batch(next_id, &length);

        if (csv) {
            printf("  Export successful: %u bytes\n", length);
            free(csv);

            /* Mark as transmitted */
            meshstatic_storage_mark_transmitted(next_id);
            printf("  ✓ Marked as transmitted\n");
        }
    } else {
        printf("  No batches available for transmission\n");
    }
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int main(void)
{
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║   Meshstatic Storage Manager Test Suite                  ║\n");
    printf("║   Component 2: Flash Storage with CSV Files              ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n");

    /* Run all tests */
    test_storage_init();
    test_save_batch();
    test_load_batch();
    test_list_batches();
    test_delete_batch();
    test_export_batch();
    test_multiple_batches();
    test_cleanup_old();
    test_next_to_transmit();

    printf("\n=== All Tests Complete ===\n");
    printf("Component 2 is ready for integration!\n\n");

    return 0;
}
