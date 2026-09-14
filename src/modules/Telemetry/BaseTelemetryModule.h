#pragma once

#include "DebugConfiguration.h"
#include "NodeDB.h"
#include "PortPolicy.h"
#include "configuration.h"
#include "sleep.h"

class BaseTelemetryModule
{
  public:
    /// May we answer a telemetry request from `from`? One policy for every sub-type.
    static bool wouldReplyToPoll(NodeNum from, uint32_t dest)
    {
        return replyPolicyAllows(moduleConfig.telemetry.policy_flags, from, dest);
    }

    /// Routine (timer-driven) send destination: 0 = broadcast, else that node.
    static NodeNum routineDest(uint32_t configured) { return configured ? (NodeNum)configured : NODENUM_BROADCAST; }

    /// Admin gate for a telemetry config: PKC_ALWAYS needs a key for every routine destination.
    static bool pkcOnlyDestsHaveKeys(const meshtastic_ModuleConfig_TelemetryConfig &t, char *why = nullptr, size_t whyLen = 0)
    {
        const uint32_t dests[] = {t.device_dest, t.environment_dest, t.air_quality_dest, t.power_dest, t.health_dest};
        return pkcAlwaysDestsHaveKeys(t.policy_flags, dests, sizeof(dests) / sizeof(dests[0]), why, whyLen);
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
