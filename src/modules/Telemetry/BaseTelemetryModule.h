#pragma once

#include "DebugConfiguration.h"
#include "NodeDB.h"
#include "configuration.h"
#include "sleep.h"

class BaseTelemetryModule
{
  public:
    /// May we answer a telemetry request from `from`? Ignored nodes are refused regardless of
    /// telemetry_flags; every flag bit only restricts, so unset answers everyone.
    static bool wouldReplyToPoll(NodeNum from, uint32_t dest)
    {
        // The flags govern who on the mesh may poll us; a request from our own node is the phone.
        if (nodeDB && from == nodeDB->getNodeNum())
            return true;
        if (nodeDB) {
            const meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(from);
            if (n && nodeInfoLiteIsIgnored(n))
                return false;
        }
        const uint32_t flags = moduleConfig.telemetry.telemetry_flags;
        if (flags & meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags_NO_ADHOC_REPLY)
            return false;
        if ((flags & meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags_REPLY_ONLY_TO_DEST) && (!dest || from != dest))
            return false;
        if ((flags & meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags_REPLY_TO_FAVOURITES_ONLY) && nodeDB &&
            !nodeDB->isFavorite(from))
            return false;
        return true;
    }

    /// Routine (timer-driven) send destination: 0 = broadcast, else that node.
    static NodeNum routineDest(uint32_t configured) { return configured ? (NodeNum)configured : NODENUM_BROADCAST; }

    /// Admin gate: with ALWAYS_PKC, every configured destination must have a public key in NodeDB,
    /// or the routine send would fail at encode on every interval.
    static bool pkcOnlyDestsHaveKeys(const meshtastic_ModuleConfig_TelemetryConfig &t, uint32_t paxcounterDest)
    {
        if (!(t.telemetry_flags & meshtastic_ModuleConfig_TelemetryConfig_TelemetryFlags_ALWAYS_PKC))
            return true;
        const uint32_t dests[] = {t.device_dest, t.environment_dest, t.air_quality_dest,
                                  t.power_dest,  t.health_dest,      paxcounterDest};
        for (uint32_t d : dests) {
            meshtastic_NodeInfoLite_public_key_t key;
            if (d && !(nodeDB && nodeDB->copyPublicKey(d, key))) {
                LOG_WARN("ALWAYS_PKC refused: no public key for destination 0x%08x", d);
                return false;
            }
        }
        return true;
    }

  protected:
    bool isSensorOrRouterRole() const
    {
        return config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR ||
               config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER ||
               config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER_LATE;
    }

    /// True for a SENSOR role node with power saving enabled, the only combination that deep
    /// sleeps between telemetry broadcasts
    bool isPowerSavingSensor() const
    {
        return config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR && config.power.is_power_saving;
    }

    /**
     * Call while a deep sleep is pending (sleepOnNextExecution): the telemetry packet queued by
     * sendTelemetry() goes out asynchronously, and sleeping while it is still queued or on air
     * truncates the transmission. Returns true if the caller should reschedule in
     * PREFLIGHT_SLEEP_RETRY_MS and check again. Bounded by MAX_PREFLIGHT_SLEEP_DEFERRALS so a
     * busy mesh can't keep the node awake forever. Reset preflightSleepDeferrals to 0 whenever
     * sleepOnNextExecution is armed.
     */
    bool shouldDeferDeepSleep()
    {
        if (doPreflightSleep(true) || preflightSleepDeferrals >= MAX_PREFLIGHT_SLEEP_DEFERRALS)
            return false;
        preflightSleepDeferrals++;
        LOG_DEBUG("Radio busy, defer deep sleep");
        return true;
    }

    // While sleepOnNextExecution is pending, counts how often the deep sleep was postponed
    // because doPreflightSleep() vetoed it (e.g. radio still transmitting)
    uint32_t preflightSleepDeferrals = 0;
};
