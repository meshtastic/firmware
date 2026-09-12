#pragma once

#include "DebugConfiguration.h"
#include "NodeDB.h"
#include "configuration.h"
#include "sleep.h"

class BaseTelemetryModule
{
  public:
    /// Whether we may answer a telemetry request from `from`, per ModuleConfig.TelemetryConfig
    /// .telemetry_flags. An ignored node is refused unconditionally - that is what ignoring means,
    /// not a policy choice - and every flag bit is a further restriction, so unset answers everyone.
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

    /// Destination for a routine (timer-driven) send. 0 means broadcast, which is the default and
    /// preserves the historic behaviour; any other value addresses that node.
    static NodeNum routineDest(uint32_t configured) { return configured ? (NodeNum)configured : NODENUM_BROADCAST; }

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
