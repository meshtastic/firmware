#pragma once

#include "concurrency/OSThread.h"
#include "configuration.h"
#include "mesh/generated/meshtastic/config.pb.h"

// Compiled in unless a variant opts out. (Undefined macro evaluates to 0 in #if, so default = on.)
#if !MESHTASTIC_EXCLUDE_ROUTER_RETIREMENT

/**
 * RouterRetirementModule - auto-demote an unattended infrastructure node down a "retirement
 * slope": ROUTER -> ROUTER_LATE -> CLIENT, one rung per ~3 months of cumulative uptime during
 * which NO admin session (remote or local) occurred. An admin session resets the credit (proof
 * the node is still managed). Opt-in (default OFF). Prevents abandoned routers clogging dense
 * meshes.
 *
 * The credit lives in its own small file (/prefs/routerRetirement.bin), written once per hourly
 * tick and on every reset, so it survives power loss without churning the shared devicestate.
 *
 * The policy is split into pure static helpers (no globals) so it is unit-testable; the OSThread
 * wiring applies it to config and the persisted credit.
 */
class RouterRetirementModule : public concurrency::OSThread
{
  public:
    RouterRetirementModule();

    /// Reset the unmanaged-uptime credit - call once an admin session (remote OR local) has passed
    /// authorization. Cheap when the credit is already zero; otherwise persists the reset at once.
    void noteAdminSession();

    /// Default cumulative-uptime threshold per demotion rung: ~3 months.
    static constexpr uint32_t DEFAULT_STEP_THRESHOLD_SECS = 90UL * 24 * 60 * 60; // 7,776,000
    /// Credit is accrued and checkpointed on this cadence.
    static constexpr uint32_t ACCRUE_INTERVAL_SECS = 60UL * 60;

    // --- Pure policy helpers (no globals; unit-testable) ---
    static bool isRetirableRole(meshtastic_Config_DeviceConfig_Role role);
    /// Next rung down the slope. Returns the input role unchanged for non-retirable roles.
    static meshtastic_Config_DeviceConfig_Role nextRetirementRole(meshtastic_Config_DeviceConfig_Role role);
    /// Configured threshold, or the default when configured == 0.
    static uint32_t effectiveThresholdSecs(uint32_t configuredSecs);
    /// Demote now? enabled + retirable role + credit has reached the threshold.
    static bool shouldRetire(bool enabled, meshtastic_Config_DeviceConfig_Role role, uint32_t creditSecs, uint32_t thresholdSecs);

  protected:
    int32_t runOnce() override;

  private:
    // On-disk record. Fixed layout, seconds only - no clock arithmetic crosses a reboot.
    struct PersistedCredit {
        uint32_t magic;
        uint16_t version;
        uint16_t reserved;
        uint32_t creditSecs;
    };
    static constexpr uint32_t CREDIT_FILE_MAGIC = 0x52525431; // "RRT1"
    static constexpr uint16_t CREDIT_FILE_VERSION = 1;
    static constexpr const char *CREDIT_FILE = "/prefs/routerRetirement.bin";

    void loadFromDisk();
    bool saveToDisk() const;
    void retireOneRung();

    uint32_t creditSecs = 0;

#ifdef PIO_UNIT_TESTING
    friend class RouterRetirementTestShim;
#endif
};

extern RouterRetirementModule *routerRetirementModule;

#endif // !MESHTASTIC_EXCLUDE_ROUTER_RETIREMENT
