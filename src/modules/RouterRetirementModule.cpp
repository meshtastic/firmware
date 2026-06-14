#include "RouterRetirementModule.h"

#if !MESHTASTIC_EXCLUDE_ROUTER_RETIREMENT

#include "NodeDB.h"
#include "Time.h"
#include "main.h" // rebootAtMsec

RouterRetirementModule *routerRetirementModule;

RouterRetirementModule::RouterRetirementModule() : concurrency::OSThread("RouterRetirement")
{
    // Runs everywhere but no-ops unless enabled AND the role is retirable (see runOnce).
}

bool RouterRetirementModule::isRetirableRole(meshtastic_Config_DeviceConfig_Role role)
{
    return role == meshtastic_Config_DeviceConfig_Role_ROUTER || role == meshtastic_Config_DeviceConfig_Role_ROUTER_LATE;
}

meshtastic_Config_DeviceConfig_Role RouterRetirementModule::nextRetirementRole(meshtastic_Config_DeviceConfig_Role role)
{
    switch (role) {
    case meshtastic_Config_DeviceConfig_Role_ROUTER:
        return meshtastic_Config_DeviceConfig_Role_ROUTER_LATE;
    case meshtastic_Config_DeviceConfig_Role_ROUTER_LATE:
        return meshtastic_Config_DeviceConfig_Role_CLIENT;
    default:
        return role; // ladder bottom / not applicable
    }
}

uint32_t RouterRetirementModule::effectiveThresholdSecs(uint32_t configuredSecs)
{
    return configuredSecs > 0 ? configuredSecs : DEFAULT_STEP_THRESHOLD_SECS;
}

bool RouterRetirementModule::shouldRetire(bool enabled, meshtastic_Config_DeviceConfig_Role role, uint32_t creditSecs,
                                          uint32_t thresholdSecs)
{
    return enabled && isRetirableRole(role) && creditSecs >= thresholdSecs;
}

void RouterRetirementModule::noteAdminSession()
{
    // An admin touched us — we're managed; restart the unmanaged clock.
    devicestate.router_retirement_credit_secs = 0;
}

void RouterRetirementModule::retireOneRung()
{
    const meshtastic_Config_DeviceConfig_Role current = config.device.role;
    const meshtastic_Config_DeviceConfig_Role next = nextRetirementRole(current);
    if (next == current)
        return; // defensive: ladder bottom

    LOG_WARN("Router retirement: demoting role %d -> %d after %u s unmanaged uptime", (int)current, (int)next,
             devicestate.router_retirement_credit_secs);
    config.device.role = next;
    devicestate.router_retirement_credit_secs = 0;            // fresh credit at the new rung
    nodeDB->installRoleDefaults(next);                        // role-appropriate intervals/rebroadcast
    nodeDB->saveToDisk(SEGMENT_CONFIG | SEGMENT_DEVICESTATE); // persist role + zeroed credit
    rebootAtMsec = Time::getMillis() + 5000;                  // reboot so the new role fully applies
}

int32_t RouterRetirementModule::runOnce()
{
    const meshtastic_ModuleConfig_RouterRetirementConfig &cfg = moduleConfig.router_retirement;

    // Dormant unless enabled and currently a retirable role.
    if (cfg.enabled && isRetirableRole(config.device.role)) {
        // Accrue this interval's uptime. Fixed increment (no millis() math) => rollover-immune.
        devicestate.router_retirement_credit_secs += RUN_INTERVAL_MS / 1000;

        if (shouldRetire(cfg.enabled, config.device.role, devicestate.router_retirement_credit_secs,
                         effectiveThresholdSecs(cfg.step_threshold_secs))) {
            retireOneRung();
        }
    }
    return RUN_INTERVAL_MS;
}

#endif // !MESHTASTIC_EXCLUDE_ROUTER_RETIREMENT
