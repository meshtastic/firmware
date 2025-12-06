/**
 * @file CORE1_INTEGRATION_SNIPPET.cpp
 * @brief Exact integration points for meshstatic in Core 1 USB capture loop
 *
 * This file shows WHERE and HOW to integrate meshstatic into the existing
 * capture_controller_core1_main_v2() function in capture_v2.cpp
 *
 * Location: /Users/rstown/Desktop/Projects/STE/client_pico/lib/USBCapture/capture_v2.cpp
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// ============================================================================
// STEP 1: Add include at top of capture_v2.cpp
// ============================================================================

#include "pio_manager.h"
#include "stats.h"
#include "packet_processor_core1.h"
#include "keyboard_decoder_core1.h"
#include "config.h"
#include "keystroke_queue.h"
#include "cpu_monitor.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/timer.h"
#include <string.h>

// ⭐ ADD THIS LINE:
#include "meshstatic_core1.h"  // Meshstatic Core 1 integration

// ============================================================================
// STEP 2: Initialize meshstatic in Core 1 startup
// ============================================================================

void capture_controller_core1_main_v2(void)
{
    while (1)
    {
        /* Wait for start command from Core0 */
        if (multicore_fifo_rvalid())
        {
            uint32_t cmd = multicore_fifo_pop_blocking();

            if (cmd == 0xDEADBEEF)
            {
                g_capture_running_v2 = false;
                continue;
            }

            if (cmd != 0x69696969)
            {
                continue;
            }

            /* Start capture */
            g_capture_running_v2 = true;

            /* Configure PIO for capture */
            pio_config_t pio_config = {0};
            bool fs = (g_capture_speed_v2 == CAPTURE_SPEED_FULL);

            if (!pio_manager_configure_capture(&pio_config, fs))
            {
                g_capture_running_v2 = false;
                multicore_fifo_push_blocking(0x69696969);
                continue;
            }

            /* Initialize keyboard decoder with queue */
            if (g_keystroke_queue_v2)
            {
                keyboard_decoder_core1_init(g_keystroke_queue_v2);
            }

            // ⭐ ADD THIS: Initialize meshstatic on Core 1
            if (!meshstatic_core1_init()) {
                // Log error but continue (meshstatic is non-critical)
                // USB capture will still work without it
            }

            /* Enable watchdog */
            watchdog_enable(4000, true);

            /* ... rest of initialization ... */

            /* Main capture and processing loop */
            while (g_capture_running_v2)
            {
                watchdog_update();

                /* Check for stop command */
                if (multicore_fifo_rvalid())
                {
                    uint32_t signal = multicore_fifo_pop_blocking();
                    if (signal == 0xDEADBEEF)
                    {
                        g_capture_running_v2 = false;
                        break;
                    }
                }

                // ... PIO FIFO reading ...
                // ... packet accumulation ...
                // ... keyboard decoding ...

                /* ============================================================
                 * STEP 3: Add meshstatic after successful keystroke decode
                 * ============================================================ */

                // After packet_processor_core1_process_packet() and
                // keyboard_decoder_core1_process_packet() have validated
                // and decoded a keystroke event:

                if (keystroke_event_valid) {
                    // Push to Core0 queue (EXISTING - keep this!)
                    // This is for LoRa transmission
                    // keystroke_queue_push(g_keystroke_queue_v2, &event);

                    // ⭐ ADD THIS: Add to meshstatic batch (NEW!)
                    // This is for CSV batch storage on Core 1
                    meshstatic_core1_add_keystroke(
                        event.scancode,
                        event.modifier,
                        event.character,
                        event.timestamp_us
                    );

                    // Note: meshstatic_core1_add_keystroke() will:
                    // - Add keystroke to CSV batch
                    // - Check if batch full (200 bytes)
                    // - If full → Save to flash (on Core 1!)
                    // - Reset batch for next collection
                    //
                    // All of this happens inline on Core 1 without blocking
                }

                // ⭐ ADD THIS: Check auto-flush timeout
                // Call this every loop iteration (or every N iterations for efficiency)
                static uint32_t flush_check_counter = 0;
                if (++flush_check_counter >= 1000) {  // Check every 1000 iterations
                    flush_check_counter = 0;
                    meshstatic_core1_check_auto_flush(time_us_64());
                    // This flushes batch if 10 seconds idle
                }

                // ... continue with capture loop ...
            }

            // ⭐ ADD THIS: Shutdown meshstatic when Core 1 exits
            meshstatic_core1_shutdown();  // Flush remaining keystrokes
        }
    }
}

// ============================================================================
// EXAMPLE: Complete Integration at Specific Location
// ============================================================================

// Around line 200 in capture_v2.cpp, after:
// keyboard_decoder_core1_process_packet(...)

if (result == KEYBOARD_DECODE_SUCCESS) {
    keystroke_event_t event;
    keyboard_decoder_core1_get_event(&event);

    // Existing: Push to Core0 queue
    keystroke_queue_push(g_keystroke_queue_v2, &event);

    // ⭐ NEW: Add to meshstatic batch (Core 1)
    meshstatic_core1_add_keystroke(
        event.scancode,
        event.modifier,
        event.character,
        event.timestamp_us
    );
}

// ============================================================================
// MINIMAL INTEGRATION (3 lines of code!)
// ============================================================================

// At Core 1 startup:
meshstatic_core1_init();

// After each keystroke decode:
meshstatic_core1_add_keystroke(scancode, modifier, character, timestamp_us);

// Before Core 1 shutdown:
meshstatic_core1_shutdown();

// That's it! Meshstatic is now fully integrated on Core 1!
