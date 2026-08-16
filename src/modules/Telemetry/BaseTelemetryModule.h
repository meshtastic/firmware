#pragma once

#include "DebugConfiguration.h"
#include "NodeDB.h"
#include "TelemetryHistory.h"
#include "configuration.h"
#include "mesh/MeshService.h"
#include "mesh/MeshTypes.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"
#include "mqtt/MQTT.h"
#include "sleep.h"

#ifndef MESHTASTIC_MAX_READINGS_PER_MESH_PACKET
#define MESHTASTIC_MAX_READINGS_PER_MESH_PACKET 10
#endif

class BaseTelemetryModule
{
  public:
    /// Where a batch of buffered telemetry readings is being published to
    enum class PublishTarget { Mesh, Mqtt };

    virtual ~BaseTelemetryModule() = default;

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

    // Telemetry publish history
    // Note: lastSentToMesh is not here: it's tracked externally
    uint32_t lastSentToPhone = 0;
    uint32_t lastSentToMqtt = 0;
    uint32_t lastRead = 0;

    virtual meshtastic_MeshPacket *allocTelemetryHistoryPacket() { return nullptr; }
    virtual meshtastic_MeshPacket *allocTelemetryPacket(const meshtastic_Telemetry &m) { return nullptr; }

    virtual void onPublishedTelemetry(const meshtastic_MeshPacket &p) {}

    bool publishTelemetry(meshtastic_Telemetry &m, NodeNum dest, bool phoneOnly);
    template <typename T, uint8_t N>
    bool publishBufferedTelemetry(TelemetryHistoryBuffer<T, N> &history, PublishTarget target, uint32_t intervalSec);
};
