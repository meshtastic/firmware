/**
 * @file MeshstaticModule.cpp
 * @brief Implementation of Meshstatic keystroke capture module
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "configuration.h"

#if defined(ARCH_RP2040) && defined(HW_VARIANT_RPIPICO2)

#include "MeshstaticModule.h"
#include "meshstatic_core1.h"
#include <Arduino.h>

// Global instance (allocated in Modules.cpp)
MeshstaticModule *meshstaticModule;

/**
 * @brief Constructor - Register with OSThread scheduler
 *
 * Period: 100ms (10 Hz execution rate for responsive keystroke capture)
 */
MeshstaticModule::MeshstaticModule()
    : concurrency::OSThread("MeshstaticModule", 100) // 100ms interval
{
    LOG_INFO("MeshstaticModule constructor called (rpipico2 variant)");
}

/**
 * @brief Initialize module on first run
 */
bool MeshstaticModule::initializeModule()
{
    LOG_INFO("Initializing MeshstaticModule...");

    // Initialize Core 1 controller (Components 1+2+3)
    if (!meshstatic_core1_init()) {
        LOG_ERROR("Failed to initialize meshstatic core1 controller");
        return false;
    }

    LOG_INFO("✓ Meshstatic module initialized successfully");
    LOG_INFO("  CSV batch format: 200-byte limit");
    LOG_INFO("  Storage: LittleFS (/meshstatic/)");
    LOG_INFO("  Auto-flush: 10 seconds idle timeout");
    LOG_INFO("  Max keystrokes per batch: ~4");

    return true;
}

/**
 * @brief Process keystrokes from USB capture queue
 *
 * This function would integrate with the actual USB capture system.
 * For now, it's a placeholder for demonstration.
 *
 * Real implementation would:
 * 1. Check USB capture queue (usbCapture->available())
 * 2. Dequeue keystroke events
 * 3. Pass to meshstatic_core1_add_keystroke()
 */
uint32_t MeshstaticModule::processKeystrokes()
{
    uint32_t processed = 0;

    // TODO: Integration with actual USB capture
    // Example integration code:
    //
    // extern USBCapture* usbCapture;
    //
    // if (usbCapture && usbCapture->available()) {
    //     keystroke_event_t event;
    //     while (usbCapture->getKeystroke(&event)) {
    //         meshstatic_core1_add_keystroke(
    //             event.scancode,
    //             event.modifier,
    //             event.character,
    //             event.capture_timestamp_us
    //         );
    //         processed++;
    //     }
    // }

    return processed;
}

/**
 * @brief Check auto-flush conditions
 */
bool MeshstaticModule::checkAutoFlush()
{
    uint64_t current_time_us = micros();

    // Check auto-flush timeout (10 seconds idle)
    return meshstatic_core1_check_auto_flush(current_time_us);
}

/**
 * @brief Print statistics (every 60 seconds)
 */
void MeshstaticModule::printStats()
{
    uint32_t now = millis();

    // Print stats every 60 seconds
    if ((now - last_stats_print) >= 60000) {
        last_stats_print = now;

        meshstatic_core1_stats_t stats;
        meshstatic_core1_get_stats(&stats);

        LOG_INFO("Meshstatic Stats: captured=%u, batches=%u, errors=%u",
                 stats.keystrokes_captured,
                 stats.batches_saved,
                 stats.save_errors);

        // Get storage stats
        meshstatic_storage_stats_t storage_stats;
        meshstatic_storage_get_stats(&storage_stats);

        LOG_INFO("  Storage: %u batches, %u bytes, oldest=%u, newest=%u",
                 storage_stats.total_batches,
                 storage_stats.total_bytes,
                 storage_stats.oldest_batch_id,
                 storage_stats.newest_batch_id);
    }
}

/**
 * @brief Periodic execution function (called by OSThread scheduler)
 *
 * Execution flow:
 * 1. First run: Initialize module
 * 2. Process keystrokes from USB capture
 * 3. Check auto-flush conditions
 * 4. Periodic statistics logging
 *
 * @return Interval in ms until next execution (RUN_SAME = keep current interval)
 */
int32_t MeshstaticModule::runOnce()
{
    // First-time initialization
    if (firstRun) {
        firstRun = false;

        LOG_INFO("MeshstaticModule first run - initializing...");

        initialized = initializeModule();

        if (!initialized) {
            LOG_ERROR("MeshstaticModule initialization failed - disabling");
            return disable();  // Disable module on init failure
        }

        return RUN_SAME;  // Continue with 100ms interval
    }

    // Skip if not initialized
    if (!initialized) {
        return disable();
    }

    // Process keystrokes from USB capture
    uint32_t processed = processKeystrokes();

    if (processed > 0) {
        keystrokes_captured += processed;
    }

    // Check auto-flush conditions
    if (checkAutoFlush()) {
        batches_saved++;
    }

    // Periodic statistics logging
    printStats();

    return RUN_SAME;  // Keep 100ms interval
}

/**
 * @brief Get module statistics
 */
void MeshstaticModule::getStats(uint32_t* keystrokes, uint32_t* batches, uint32_t* errors)
{
    if (keystrokes) *keystrokes = keystrokes_captured;
    if (batches) *batches = batches_saved;
    if (errors) *errors = save_errors;
}

#endif // ARCH_RP2040 && HW_VARIANT_RPIPICO2
