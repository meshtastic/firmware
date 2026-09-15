#include "RouterRetirementModule.h"

#if !MESHTASTIC_EXCLUDE_ROUTER_RETIREMENT

#include "Default.h"
#include "FSCommon.h"
#include "NodeDB.h"
#include "SPILock.h"
#include "SafeFile.h"
#include "UptimeClock.h"
#include "concurrency/LockGuard.h"
#include "main.h" // rebootAtMsec

RouterRetirementModule *routerRetirementModule;

RouterRetirementModule::RouterRetirementModule() : concurrency::OSThread("RouterRetirement")
{
    // Runs everywhere but no-ops unless the role is retirable (see runOnce).
    loadFromDisk();
    // A fresh thread fires at once; the first hour has to elapse before it counts.
    setIntervalFromNow(ACCRUE_INTERVAL_SECS * 1000);
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

uint32_t RouterRetirementModule::effectiveThresholdSecs(uint32_t configuredWeeks)
{
    if (configuredWeeks == 0)
        return DEFAULT_STEP_THRESHOLD_SECS;
    // ~7101 weeks overflows uint32 seconds; clamp to the ceiling rather than wrap to a short threshold.
    if (configuredWeeks > UINT32_MAX / WEEK_SECS)
        return UINT32_MAX;
    return configuredWeeks * WEEK_SECS;
}

bool RouterRetirementModule::shouldRetire(meshtastic_Config_DeviceConfig_Role role, uint32_t creditSecs, uint32_t thresholdSecs)
{
    return isRetirableRole(role) && creditSecs >= thresholdSecs;
}

void RouterRetirementModule::loadFromDisk()
{
#ifdef FSCom
    concurrency::LockGuard g(spiLock);
    auto file = FSCom.open(CREDIT_FILE, FILE_O_READ);
    if (!file)
        return;
    PersistedCredit rec{};
    const bool readOk = file.read(reinterpret_cast<uint8_t *>(&rec), sizeof(rec)) == sizeof(rec);
    file.close();
    if (!readOk || rec.magic != CREDIT_FILE_MAGIC || rec.version != CREDIT_FILE_VERSION) {
        LOG_WARN("Router retirement: invalid credit file (magic=%08x ver=%u), starting from 0", rec.magic, rec.version);
        return;
    }
    creditSecs = rec.creditSecs;
    LOG_INFO("Router retirement: loaded %u s unmanaged uptime credit", creditSecs);
#endif
}

bool RouterRetirementModule::saveToDisk() const
{
#ifdef FSCom
    FSCom.mkdir("/prefs");
    PersistedCredit rec{};
    rec.magic = CREDIT_FILE_MAGIC;
    rec.version = CREDIT_FILE_VERSION;
    rec.creditSecs = creditSecs;
    auto file = SafeFile(CREDIT_FILE, true);
    const size_t written = file.write(reinterpret_cast<const uint8_t *>(&rec), sizeof(rec));
    if (file.close() && written == sizeof(rec))
        return true;
    LOG_WARN("Router retirement: failed to write %s", CREDIT_FILE);
    return false;
#else
    return true;
#endif
}

void RouterRetirementModule::noteAdminSession()
{
    concurrency::LockGuard g(&lock);
    // An admin touched us - we're managed; restart the unmanaged clock. Nothing to do (and no
    // flash write) when it never started.
    if (creditSecs == 0)
        return;
    creditSecs = 0;
    saveToDisk();
}

void RouterRetirementModule::restoreClientDefaults()
{
    // The ROUTER rung ran the interval installers under IF_ROUTER; run them again as CLIENT.
    nodeDB->initConfigIntervals();
    nodeDB->initModuleConfigIntervals();
    moduleConfig.telemetry.device_update_interval = default_telemetry_broadcast_interval_secs;
    if (config.device.rebroadcast_mode == meshtastic_Config_DeviceConfig_RebroadcastMode_CORE_PORTNUMS_ONLY)
        config.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_ALL;
    owner.has_is_unmessagable = true;
    owner.is_unmessagable = false;
}

bool RouterRetirementModule::commitRetirement()
{
    // Role defaults touch config, module config (telemetry interval) and the owner (devicestate).
    if (!nodeDB->saveToDisk(SEGMENT_CONFIG | SEGMENT_MODULECONFIG | SEGMENT_DEVICESTATE)) {
        LOG_WARN("Router retirement: role save failed, will retry next tick");
        retirementSavePending = true;
        return false;
    }
    retirementSavePending = false;
    rebootAtMsec = Time::getMillis() + 5000; // reboot so the new role fully applies
    return true;
}

void RouterRetirementModule::retireOneRung()
{
    const meshtastic_Config_DeviceConfig_Role current = config.device.role;
    const meshtastic_Config_DeviceConfig_Role next = nextRetirementRole(current);
    if (next == current)
        return; // defensive: ladder bottom

    LOG_WARN("Router retirement: demoting role %d -> %d after %u s unmanaged uptime", (int)current, (int)next, creditSecs);
    config.device.role = next;
    creditSecs = 0; // fresh credit at the new rung
    saveToDisk();
    if (next == meshtastic_Config_DeviceConfig_Role_CLIENT)
        restoreClientDefaults(); // installRoleDefaults has no CLIENT branch
    else
        nodeDB->installRoleDefaults(next);
    commitRetirement();
}

int32_t RouterRetirementModule::runOnce()
{
    concurrency::LockGuard g(&lock);
    const meshtastic_ModuleConfig_RouterRetirementConfig &cfg = moduleConfig.router_retirement;

    // A demotion is applied in RAM but not yet on flash: land it before anything else.
    if (retirementSavePending) {
        commitRetirement();
        return ACCRUE_INTERVAL_SECS * 1000;
    }

    // Dormant unless currently a retirable role.
    if (isRetirableRole(config.device.role)) {
        // Accrue this interval's uptime as a fixed count of seconds: no clock arithmetic, so
        // neither a rollover nor a reboot can inflate it. Checkpointed every tick.
        if (creditSecs < UINT32_MAX - ACCRUE_INTERVAL_SECS)
            creditSecs += ACCRUE_INTERVAL_SECS;

        if (shouldRetire(config.device.role, creditSecs, effectiveThresholdSecs(cfg.step_threshold_weeks)))
            retireOneRung();
        else
            saveToDisk();
    }
    return ACCRUE_INTERVAL_SECS * 1000;
}

#endif // !MESHTASTIC_EXCLUDE_ROUTER_RETIREMENT
