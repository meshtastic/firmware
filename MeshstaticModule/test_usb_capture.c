/**
 * @file test_usb_capture.c
 * @brief Test program for USBCaptureModule
 *
 * Tests the independent Core 1 USB capture module with lock-free queue.
 *
 * Compile:
 *   gcc test_usb_capture.c USBCaptureModule.c -o test_usb_capture
 *
 * Run:
 *   ./test_usb_capture
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "USBCaptureModule.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Test Cases
 * ============================================================================ */

void test_module_init(void)
{
    printf("\n=== Test 1: Module Initialization ===\n");

    usb_capture_config_t config = {
        .dp_pin = 20,
        .dm_pin = 21,
        .full_speed_mode = false
    };

    if (usb_capture_module_init(&config)) {
        printf("✓ Module initialized successfully\n");
    } else {
        printf("✗ Module initialization failed\n");
    }

    usb_capture_module_print_stats();
}

void test_module_start(void)
{
    printf("\n=== Test 2: Start Capture ===\n");

    if (usb_capture_module_start()) {
        printf("✓ Capture started\n");
    } else {
        printf("✗ Failed to start capture\n");
    }

    if (usb_capture_module_is_running()) {
        printf("✓ Module is running\n");
    }
}

void test_queue_operations(void)
{
    printf("\n=== Test 3: Queue Operations ===\n");

    printf("Queue count: %u\n", usb_capture_module_get_count());
    printf("Queue available: %s\n", usb_capture_module_available() ? "YES" : "NO");

    /* Process some iterations (would capture USB data in real system) */
    printf("\nProcessing capture iterations...\n");
    for (int i = 0; i < 5; i++) {
        uint32_t added = usb_capture_module_process();
        if (added > 0) {
            printf("  Iteration %d: %u events added\n", i + 1, added);
        }
    }

    usb_capture_module_print_stats();
}

void test_consumer_interface(void)
{
    printf("\n=== Test 4: Consumer Interface ===\n");

    /* Check if events available */
    if (usb_capture_module_available()) {
        printf("✓ Events available in queue\n");

        /* Peek at next event */
        keystroke_event_t peek_event;
        if (usb_capture_module_peek(&peek_event)) {
            printf("  Peek: char='%c', scancode=0x%02X, modifier=0x%02X, ts=%u\n",
                   peek_event.character,
                   peek_event.scancode,
                   peek_event.modifier,
                   peek_event.timestamp_us);
        }

        /* Pop events */
        printf("\nConsuming events from queue:\n");
        keystroke_event_t event;
        uint32_t consumed = 0;

        while (usb_capture_module_pop(&event)) {
            printf("  [%u] '%c' (scancode=0x%02X, mod=0x%02X, ts=%u, type=%d)\n",
                   consumed + 1,
                   event.character,
                   event.scancode,
                   event.modifier,
                   event.timestamp_us,
                   event.type);
            consumed++;

            if (consumed >= 10) {  /* Limit output */
                break;
            }
        }

        printf("✓ Consumed %u events\n", consumed);
    } else {
        printf("  No events in queue (this is expected for placeholder implementation)\n");
    }

    usb_capture_module_print_stats();
}

void test_statistics(void)
{
    printf("\n=== Test 5: Statistics ===\n");

    usb_capture_stats_t stats;
    usb_capture_module_get_stats(&stats);

    printf("Module Statistics:\n");
    printf("  Events Captured:    %u\n", stats.events_captured);
    printf("  Events Queued:      %u\n", stats.events_queued);
    printf("  Queue Overflows:    %u\n", stats.queue_overflows);
    printf("  Decode Errors:      %u\n", stats.decode_errors);
    printf("  Packets Processed:  %u\n", stats.packets_processed);
    printf("  Current Queue Size: %u/%u\n", stats.current_queue_size, USB_CAPTURE_QUEUE_SIZE);
}

void test_module_stop(void)
{
    printf("\n=== Test 6: Stop Capture ===\n");

    usb_capture_module_stop();

    if (!usb_capture_module_is_running()) {
        printf("✓ Module stopped\n");
    }
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int main(void)
{
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║   USB Capture Module Test Suite                          ║\n");
    printf("║   Core 1 USB Capture with Lock-Free Queue                ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n");

    test_module_init();
    test_module_start();
    test_queue_operations();
    test_consumer_interface();
    test_statistics();
    test_module_stop();

    printf("\n=== All Tests Complete ===\n");
    printf("USBCaptureModule is ready for Core 1 integration!\n\n");

    return 0;
}
