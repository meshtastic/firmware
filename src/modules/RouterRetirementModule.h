#pragma once

#include "concurrency/Lock.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "mesh/generated/meshtastic/config.pb.h"

// Compiled in unless a variant opts out. (Undefined macro evaluates to 0 in #if, so default = on.)
#if !MESHTASTIC_EXCLUDE_ROUTER_RETIREMENT

/**
 * RouterRetirementModule - auto-demote an unattended infrastructure node down a "retirement
 * slope": ROUTER -> ROUTER_LATE -> CLIENT, one rung per 52 weeks (default) of cumulative uptime during
 * which NO admin session (remote or local) occurred. An admin session resets the credit (proof
 * the node is still managed). Always on. Prevents abandoned routers clogging dense meshes.
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

    /// Default cumulative-uptime threshold per demotion rung: a year, in the config's unit.
    static constexpr uint32_t DEFAULT_STEP_THRESHOLD_WEEKS = 52;
    static constexpr uint32_t WEEK_SECS = 7UL * 24 * 60 * 60;
    static constexpr uint32_t DEFAULT_STEP_THRESHOLD_SECS = DEFAULT_STEP_THRESHOLD_WEEKS * WEEK_SECS; // 31,449,600
    /// Credit is accrued and checkpointed on this cadence.
    static constexpr uint32_t ACCRUE_INTERVAL_SECS = 60UL * 60;

    // --- Pure policy helpers (no globals; unit-testable) ---
    static bool isRetirableRole(meshtastic_Config_DeviceConfig_Role role);
    /// Next rung down the slope. Returns the input role unchanged for non-retirable roles.
    static meshtastic_Config_DeviceConfig_Role nextRetirementRole(meshtastic_Config_DeviceConfig_Role role);
    /// Configured threshold in seconds (the default when configured == 0); saturates rather than wrapping.
    static uint32_t effectiveThresholdSecs(uint32_t configuredWeeks);
    /// Demote now? retirable role + credit has reached the threshold.
    static bool shouldRetire(meshtastic_Config_DeviceConfig_Role role, uint32_t creditSecs, uint32_t thresholdSecs);

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
    /// Persist the demoted role; on success schedule the reboot that applies it.
    bool commitRetirement();
    /// Undo what installRoleDefaults(ROUTER/ROUTER_LATE) set, now that the role is CLIENT.
    static void restoreClientDefaults();

    /// Serialises the credit against noteAdminSession(), which the admin path may call from another task.
    concurrency::Lock lock;
    uint32_t creditSecs = 0;
    /// A demotion whose config save failed (unsafe power); the next tick retries before rebooting.
    bool retirementSavePending = false;

#ifdef PIO_UNIT_TESTING
    friend class RouterRetirementTestShim;
#endif
};

extern RouterRetirementModule *routerRetirementModule;

#endif // !MESHTASTIC_EXCLUDE_ROUTER_RETIREMENT
