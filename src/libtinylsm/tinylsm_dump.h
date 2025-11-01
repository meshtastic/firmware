#pragma once

#include "tinylsm_store.h"
#include <cstdint>

namespace meshtastic
{
namespace tinylsm
{

// ============================================================================
// LSM Dump/Restore for Flash Space Management
// ============================================================================

class LSMDumpManager
{
  public:
    // Dump LSM data to make room for firmware update
    // Returns bytes freed
    static size_t dumpForFirmwareUpdate();

    // Check if we should dump LSM (e.g., USB connected on nRF52)
    static bool shouldDump();

    // Clear all LSM data (emergency flash space recovery)
    static bool clearAll();

    // Get current LSM flash usage
    static size_t getFlashUsage();
};

} // namespace tinylsm
} // namespace meshtastic
