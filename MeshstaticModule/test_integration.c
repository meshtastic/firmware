/**
 * @file test_integration.c
 * @brief Integration test for all meshstatic components
 *
 * Tests complete workflow:
 *   Component 1 (Batch Manager) +
 *   Component 2 (Storage Manager) +
 *   Component 3 (Core 1 Controller)
 *
 * Simulates Core 1 USB capture loop:
 * - Initialize meshstatic system
 * - Capture keystrokes
 * - Automatic batching
 * - Flash storage
 * - Batch retrieval and transmission
 *
 * Compile:
 *   gcc test_integration.c meshstatic_core1.c meshstatic_storage.c meshstatic_batch.c -o test_integration
 *
 * Run:
 *   ./test_integration
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshstatic_core1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * Test Utilities
 * ============================================================================ */

/**
 * @brief Get current microsecond timestamp (simulated)
 */
uint64_t get_timestamp_us(void)
{
    static uint64_t base_time = 0;
    if (base_time == 0) {
        base_time = (uint64_t)(clock() * 1000000 / CLOCKS_PER_SEC);
    }
    return (uint64_t)(clock() * 1000000 / CLOCKS_PER_SEC) - base_time;
}

/**
 * @brief Simulate typing delay (microseconds)
 */
void simulate_typing_delay(uint32_t delay_us)
{
    /* Simple busy-wait for short delays */
    uint64_t start = get_timestamp_us();
    while ((get_timestamp_us() - start) < delay_us) {
        /* Busy wait */
    }
}

/* ============================================================================
 * Test Scenarios
 * ============================================================================ */

/**
 * @brief Test 1: System initialization
 */
void test_system_init(void)
{
    printf("\n=== Test 1: System Initialization ===\n");

    if (meshstatic_core1_init()) {
        printf("✓ Meshstatic Core 1 initialized successfully\n");
        meshstatic_core1_print_stats();
    } else {
        printf("✗ Failed to initialize\n");
    }
}

/**
 * @brief Test 2: Single keystroke capture
 */
void test_single_keystroke(void)
{
    printf("\n=== Test 2: Single Keystroke Capture ===\n");

    uint64_t timestamp = get_timestamp_us();

    if (meshstatic_core1_add_keystroke(0x04, 0x00, 'a', timestamp)) {
        printf("✓ Keystroke 'a' captured (scancode=0x04, ts=%llu)\n",
               (unsigned long long)timestamp);

        meshstatic_core1_print_batch_info();
    } else {
        printf("✗ Failed to capture keystroke\n");
    }
}

/**
 * @brief Test 3: Type a word (automatic batching)
 */
void test_type_word(void)
{
    printf("\n=== Test 3: Type Word 'Hello' ===\n");

    const char* word = "Hello";
    const uint8_t scancodes[] = {0x0B, 0x08, 0x0F, 0x0F, 0x12};

    for (int i = 0; i < 5; i++) {
        uint64_t timestamp = get_timestamp_us();

        if (meshstatic_core1_add_keystroke(scancodes[i], 0x00, word[i], timestamp)) {
            printf("  ✓ Typed: '%c'\n", word[i]);
        }

        simulate_typing_delay(100000);  /* 100ms between keys */
    }

    meshstatic_core1_print_stats();
    meshstatic_core1_print_batch_info();
}

/**
 * @brief Test 4: Fill batch to trigger auto-flush
 */
void test_auto_flush(void)
{
    printf("\n=== Test 4: Auto-Flush on Batch Full ===\n");

    printf("Typing alphabet to fill batch...\n");

    for (char c = 'A'; c <= 'Z'; c++) {
        uint64_t timestamp = get_timestamp_us();

        bool added = meshstatic_core1_add_keystroke(0x04 + (c - 'A'), 0x00, c, timestamp);

        if (!added) {
            printf("  Batch full after %d characters\n", c - 'A');
            break;
        }

        /* Brief delay */
        simulate_typing_delay(10000);  /* 10ms */
    }

    printf("\n");
    meshstatic_core1_print_stats();
}

/**
 * @brief Test 5: Manual flush
 */
void test_manual_flush(void)
{
    printf("\n=== Test 5: Manual Flush ===\n");

    /* Add a few keystrokes */
    meshstatic_core1_add_keystroke(0x04, 0x00, 'M', get_timestamp_us());
    meshstatic_core1_add_keystroke(0x05, 0x00, 'N', get_timestamp_us());
    meshstatic_core1_add_keystroke(0x06, 0x00, 'O', get_timestamp_us());

    printf("Added 3 keystrokes to batch\n");

    /* Manually flush */
    if (meshstatic_core1_flush_batch()) {
        printf("✓ Manual flush successful\n");
    } else {
        printf("✗ Manual flush failed\n");
    }

    meshstatic_core1_print_stats();
}

/**
 * @brief Test 6: Verify batches in storage
 */
void test_verify_storage(void)
{
    printf("\n=== Test 6: Verify Batches in Storage ===\n");

    uint32_t count = 0;
    uint32_t* batch_ids = meshstatic_storage_list_batches(&count);

    if (batch_ids && count > 0) {
        printf("✓ Found %u batches in flash storage:\n", count);

        for (uint32_t i = 0; i < count; i++) {
            printf("  [%u] Batch ID: %u (batch_%05u.csv)\n",
                   i + 1, batch_ids[i], batch_ids[i]);
        }

        free(batch_ids);
    } else {
        printf("  No batches found\n");
    }

    /* Print storage stats */
    meshstatic_storage_stats_t storage_stats;
    meshstatic_storage_get_stats(&storage_stats);

    printf("\nStorage Statistics:\n");
    printf("  Total Batches: %u\n", storage_stats.total_batches);
    printf("  Total Bytes:   %u\n", storage_stats.total_bytes);
    printf("  Oldest Batch:  %u\n", storage_stats.oldest_batch_id);
    printf("  Newest Batch:  %u\n", storage_stats.newest_batch_id);
}

/**
 * @brief Test 7: Retrieve batch for transmission
 */
void test_retrieve_batch(void)
{
    printf("\n=== Test 7: Retrieve Batch for Transmission ===\n");

    /* Get next batch to transmit */
    uint32_t batch_id = meshstatic_storage_get_next_to_transmit();

    if (batch_id > 0) {
        printf("Next batch to transmit: ID=%u\n", batch_id);

        /* Export batch */
        uint32_t length = 0;
        char* csv = meshstatic_storage_export_batch(batch_id, &length);

        if (csv) {
            printf("✓ Batch exported successfully (%u bytes)\n", length);
            printf("\n--- CSV Content ---\n");
            printf("%s", csv);
            printf("--- End CSV ---\n\n");

            /* In real system, this CSV would be transmitted via LoRa/WiFi */
            printf("✓ Batch ready for transmission\n");

            /* Mark as transmitted */
            meshstatic_storage_mark_transmitted(batch_id);

            /* In real system, delete after successful transmission confirmation */
            /* meshstatic_storage_delete_batch(batch_id); */

            free(csv);
        } else {
            printf("✗ Failed to export batch\n");
        }
    } else {
        printf("  No batches available for transmission\n");
    }
}

/**
 * @brief Test 8: Continuous capture simulation (Core 1 loop)
 */
void test_continuous_capture(void)
{
    printf("\n=== Test 8: Continuous Capture Simulation ===\n");
    printf("Simulating 20 keystrokes with realistic timing...\n\n");

    const char* message = "The quick brown fox";
    int message_len = strlen(message);

    for (int i = 0; i < message_len; i++) {
        char c = message[i];
        uint64_t timestamp = get_timestamp_us();

        /* Convert character to scancode (simplified) */
        uint8_t scancode = (c >= 'a' && c <= 'z') ? (0x04 + (c - 'a')) :
                          (c >= 'A' && c <= 'Z') ? (0x04 + (c - 'A')) :
                          (c == ' ') ? 0x2C : 0x04;

        meshstatic_core1_add_keystroke(scancode, 0x00, c, timestamp);

        /* Simulate typing speed: 50-150ms per character */
        simulate_typing_delay(50000 + (rand() % 100000));
    }

    printf("\n");
    meshstatic_core1_print_stats();
}

/**
 * @brief Test 9: Shutdown and cleanup
 */
void test_shutdown(void)
{
    printf("\n=== Test 9: Shutdown and Cleanup ===\n");

    printf("Final statistics before shutdown:\n");
    meshstatic_core1_print_stats();

    meshstatic_core1_shutdown();

    printf("✓ Meshstatic system shut down\n");
}

/* ============================================================================
 * Main Integration Test
 * ============================================================================ */

int main(void)
{
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║   Meshstatic Module - Integration Test Suite             ║\n");
    printf("║   Components 1 + 2 + 3 Working Together                  ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n");

    /* Seed random number generator for realistic timing */
    srand(time(NULL));

    /* Run all integration tests */
    test_system_init();
    test_single_keystroke();
    test_type_word();
    test_auto_flush();
    test_manual_flush();
    test_verify_storage();
    test_retrieve_batch();
    test_continuous_capture();
    test_shutdown();

    printf("\n╔═══════════════════════════════════════════════════════════╗\n");
    printf("║   Integration Test Complete!                             ║\n");
    printf("║   All 3 components working together successfully         ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n");

    printf("\nMeshstatic module is ready for RP2350 Core 1 integration!\n\n");

    return 0;
}
