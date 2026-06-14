#pragma once

#include "concurrency/OSThread.h"
#include "configuration.h"
#include "mesh/generated/meshtastic/config.pb.h"

// Compiled in unless a variant opts out. (Undefined macro evaluates to 0 in #if, so default = on.)
#if !MESHTASTIC_EXCLUDE_ROUTER_RETIREMENT

/**
 * RouterRetirementModule — auto-demote an unattended infrastructure node down a "retirement
 * slope": ROUTER -> ROUTER_LATE -> CLIENT, one rung per ~3 months of cumulative uptime during
 * which NO admin session (remote or local) occurred. An admin session resets the credit (proof
 * the node is still managed). Opt-in (default OFF). Prevents abandoned routers clogging dense
 * meshes. See .notes/router-retirement-proto.md.
 *
 * The policy is split into pure static helpers (no globals) so it is unit-testable with the
 * injected Time:: clock; the OSThread wiring applies it to config/devicestate.
 */
class RouterRetirementModule : public concurrency::OSThread
{
  public:
    RouterRetirementModule();

    /// Reset the unmanaged-uptime credit — call when any admin session (remote OR local) is
    /// authenticated. Safe to call frequently; just zeroes the persisted counter.
    void noteAdminSession();

    /// Default cumulative-uptime threshold per demotion rung: ~3 months.
    static constexpr uint32_t DEFAULT_STEP_THRESHOLD_SECS = 90UL * 24 * 60 * 60; // 7,776,000

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
    void retireOneRung();
    static constexpr uint32_t RUN_INTERVAL_MS = 60UL * 60 * 1000; // accrue hourly
};

extern RouterRetirementModule *routerRetirementModule;

#endif // !MESHTASTIC_EXCLUDE_ROUTER_RETIREMENT
