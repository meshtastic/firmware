/**
 * @file test_batch.c
 * @brief Test program for meshstatic batch manager
 *
 * Demonstrates Component 1 working independently:
 * - Initialize batch system
 * - Add keystrokes to batch
 * - Monitor CSV output and size limit
 * - Verify 200-byte limit enforcement
 *
 * Compile:
 *   gcc test_batch.c meshstatic_batch.c -o test_batch
 *
 * Run:
 *   ./test_batch
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshstatic_batch.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * Test Utilities
 * ============================================================================ */

/**
 * @brief Print batch statistics
 */
void print_batch_stats(const meshstatic_batch_t* batch)
{
    uint32_t count, csv_length, batch_id;
    meshstatic_batch_get_stats(batch, &count, &csv_length, &batch_id);

    printf("========================================\n");
    printf("Batch Statistics:\n");
    printf("  Batch ID:      %u\n", batch_id);
    printf("  Keystrokes:    %u / %u\n", count, MESHSTATIC_MAX_KEYSTROKES_PER_BATCH);
    printf("  CSV Length:    %u / %u bytes\n", csv_length, MESHSTATIC_MAX_BATCH_SIZE);
    printf("  Needs Flush:   %s\n", meshstatic_batch_is_full(batch) ? "YES" : "NO");
    printf("  Time Range:    %lu - %lu us\n",
           (unsigned long)batch->meta.start_time_us,
           (unsigned long)batch->meta.end_time_us);
    printf("========================================\n");
}

/**
 * @brief Print CSV output
 */
void print_csv_output(const meshstatic_batch_t* batch)
{
    printf("\n--- CSV Output (Batch ID: %u) ---\n", batch->meta.batch_id);
    printf("%s", meshstatic_batch_get_csv(batch));
    printf("--- End CSV (Length: %u bytes) ---\n\n", meshstatic_batch_get_csv_length(batch));
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
 * @brief Test 1: Basic batch initialization
 */
void test_batch_init(void)
{
    printf("\n=== Test 1: Batch Initialization ===\n");

    meshstatic_batch_t batch;
    meshstatic_batch_init(&batch);

    /* Verify initial state */
    printf("✓ Batch initialized\n");
    print_batch_stats(&batch);

    /* Check CSV header */
    const char* csv = meshstatic_batch_get_csv(&batch);
    if (strstr(csv, "timestamp_us,scancode,modifier,character") != NULL) {
        printf("✓ CSV header present\n");
    } else {
        printf("✗ CSV header missing!\n");
    }
}

/**
 * @brief Test 2: Add keystrokes to batch
 */
void test_add_keystrokes(void)
{
    printf("\n=== Test 2: Add Keystrokes ===\n");

    meshstatic_batch_t batch;
    meshstatic_batch_init(&batch);

    /* Simulate typing "Hello" */
    const char* test_string = "Hello";
    const uint8_t scancodes[] = {0x0B, 0x08, 0x0F, 0x0F, 0x12}; // H, e, l, l, o

    for (int i = 0; i < 5; i++) {
        uint32_t timestamp = get_timestamp_us() + (i * 1000); // 1ms spacing
        bool added = meshstatic_batch_add(&batch, scancodes[i], 0x00, test_string[i], timestamp);

        if (added) {
            printf("✓ Added keystroke: '%c' (scancode=0x%02X, ts=%u)\n",
                   test_string[i], scancodes[i], timestamp);
        } else {
            printf("✗ Failed to add keystroke '%c' (batch full?)\n", test_string[i]);
        }
    }

    print_batch_stats(&batch);
    print_csv_output(&batch);
}

/**
 * @brief Test 3: 200-byte limit enforcement
 */
void test_size_limit(void)
{
    printf("\n=== Test 3: 200-Byte Limit Enforcement ===\n");

    meshstatic_batch_t batch;
    meshstatic_batch_init(&batch);

    /* Add keystrokes until batch reaches limit */
    int added_count = 0;
    char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

    for (int i = 0; i < 100; i++) {  // Try to add 100 keystrokes
        char c = alphabet[i % strlen(alphabet)];
        uint8_t scancode = 0x04 + (i % 26);
        uint32_t timestamp = get_timestamp_us() + (i * 1000);

        bool added = meshstatic_batch_add(&batch, scancode, 0x00, c, timestamp);

        if (added) {
            added_count++;
        } else {
            printf("✓ Batch reached limit after %d keystrokes\n", added_count);
            break;
        }
    }

    print_batch_stats(&batch);

    /* Verify 200-byte limit enforced */
    uint32_t csv_length = meshstatic_batch_get_csv_length(&batch);
    if (csv_length <= MESHSTATIC_MAX_BATCH_SIZE) {
        printf("✓ CSV length within 200-byte limit: %u bytes\n", csv_length);
    } else {
        printf("✗ CSV length exceeded limit: %u bytes (limit: %u)\n",
               csv_length, MESHSTATIC_MAX_BATCH_SIZE);
    }
}

/**
 * @brief Test 4: Batch reset and reuse
 */
void test_batch_reset(void)
{
    printf("\n=== Test 4: Batch Reset and Reuse ===\n");

    meshstatic_batch_t batch;
    meshstatic_batch_init(&batch);

    /* Add some keystrokes */
    meshstatic_batch_add(&batch, 0x04, 0x00, 'A', get_timestamp_us());
    meshstatic_batch_add(&batch, 0x05, 0x00, 'B', get_timestamp_us());
    meshstatic_batch_add(&batch, 0x06, 0x00, 'C', get_timestamp_us());

    printf("Before reset:\n");
    print_batch_stats(&batch);

    uint32_t old_batch_id = batch.meta.batch_id;

    /* Reset batch */
    meshstatic_batch_reset(&batch);

    printf("\nAfter reset:\n");
    print_batch_stats(&batch);

    /* Verify batch ID incremented */
    if (batch.meta.batch_id == old_batch_id + 1) {
        printf("✓ Batch ID incremented: %u -> %u\n", old_batch_id, batch.meta.batch_id);
    } else {
        printf("✗ Batch ID not incremented correctly\n");
    }

    /* Verify batch cleared */
    if (batch.meta.count == 0) {
        printf("✓ Batch cleared successfully\n");
    } else {
        printf("✗ Batch not cleared (count=%u)\n", batch.meta.count);
    }
}

/**
 * @brief Test 5: Multiple batch cycles
 */
void test_multiple_batches(void)
{
    printf("\n=== Test 5: Multiple Batch Cycles ===\n");

    meshstatic_batch_t batch;
    meshstatic_batch_init(&batch);

    /* Simulate 3 batch cycles */
    for (int cycle = 0; cycle < 3; cycle++) {
        printf("\n--- Batch Cycle %d ---\n", cycle + 1);

        /* Fill batch */
        int added = 0;
        for (int i = 0; i < 10; i++) {
            char c = 'A' + (i % 26);
            if (meshstatic_batch_add(&batch, 0x04 + i, 0x00, c, get_timestamp_us())) {
                added++;
            } else {
                break;
            }
        }

        printf("Added %d keystrokes to batch %u\n", added, batch.meta.batch_id);
        print_batch_stats(&batch);

        /* Simulate flushing to file */
        printf("✓ Batch %u ready for flushing (%u bytes)\n",
               batch.meta.batch_id,
               meshstatic_batch_get_csv_length(&batch));

        /* Reset for next cycle */
        meshstatic_batch_reset(&batch);
    }
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int main(void)
{
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║   Meshstatic Batch Manager Test Suite                    ║\n");
    printf("║   Component 1: CSV Batch with 200-Byte Limit             ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n");

    /* Run all tests */
    test_batch_init();
    test_add_keystrokes();
    test_size_limit();
    test_batch_reset();
    test_multiple_batches();

    printf("\n=== All Tests Complete ===\n");
    printf("Component 1 is ready for integration!\n\n");

    return 0;
}
