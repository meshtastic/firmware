/**
 * @file MeshstaticModule.h
 * @brief Keystroke capture and CSV batch storage module for RP2350 Core 1
 *
 * Purpose: Captures USB keystrokes, batches them into CSV format, and saves
 * to flash storage for later transmission.
 *
 * Architecture:
 * - Runs as OSThread on Core 1 (RP2350 only)
 * - Independent operation from mesh networking
 * - 200-byte CSV batch files with automatic flushing
 * - LittleFS flash storage for persistence
 *
 * Integration:
 * - Add to Modules.cpp setupModules() for rpipico2 variant
 * - Uses existing USB capture infrastructure
 * - Coordinator will call via standard Meshtastic module lifecycle
 *
 * Board Support:
 * - RP2350 (rpipico2 variant) ONLY
 * - Requires: LittleFS, dual-core support, USB host capability
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "configuration.h"

// Only compile for RP2350 variants (rpipico2, rpipico2w)
#if defined(ARCH_RP2040) && defined(HW_VARIANT_RPIPICO2)

#include "concurrency/OSThread.h"
#include <Arduino.h>

/**
 * @brief Meshstatic Module - Keystroke Capture and Storage
 *
 * Inherits from OSThread for periodic execution on Core 1.
 * Does NOT inherit from MeshModule since it doesn't process mesh packets.
 *
 * Lifecycle:
 * 1. Constructor: Called during setupModules()
 * 2. First runOnce(): Initialize storage, USB capture integration
 * 3. Periodic runOnce(): Process keystrokes, flush batches
 * 4. Shutdown: Flush remaining data, cleanup
 */
class MeshstaticModule : private concurrency::OSThread
{
  private:
    bool firstRun = true;
    bool initialized = false;

    // Statistics
    uint32_t keystrokes_captured = 0;
    uint32_t batches_saved = 0;
    uint32_t save_errors = 0;
    uint32_t last_stats_print = 0;

    // Auto-flush tracking
    uint64_t last_keystroke_us = 0;

  public:
    /**
     * @brief Constructor
     *
     * Registers module with mainController for periodic execution.
     * Actual initialization happens in first runOnce() call.
     */
    MeshstaticModule();

    /**
     * @brief Get module statistics
     *
     * @param keystrokes Output: total keystrokes captured
     * @param batches Output: total batches saved
     * @param errors Output: save error count
     */
    void getStats(uint32_t* keystrokes, uint32_t* batches, uint32_t* errors);

  protected:
    /**
     * @brief Periodic execution function
     *
     * Called by OSThread scheduler at regular intervals.
     *
     * First run:
     * - Initialize storage system
     * - Set up batch manager
     * - Log module startup
     *
     * Subsequent runs:
     * - Process captured keystrokes from USB queue
     * - Check for batch flush conditions
     * - Update statistics
     *
     * @return Interval in ms until next execution (or RUN_SAME)
     */
    virtual int32_t runOnce() override;

  private:
    /**
     * @brief Initialize module on first run
     *
     * @return true if initialization successful
     */
    bool initializeModule();

    /**
     * @brief Process keystroke capture and batching
     *
     * @return Number of keystrokes processed
     */
    uint32_t processKeystrokes();

    /**
     * @brief Check and handle auto-flush conditions
     *
     * @return true if batch was flushed
     */
    bool checkAutoFlush();

    /**
     * @brief Print module statistics (periodic logging)
     */
    void printStats();
};

extern MeshstaticModule *meshstaticModule;

#endif // ARCH_RP2040 && HW_VARIANT_RPIPICO2
