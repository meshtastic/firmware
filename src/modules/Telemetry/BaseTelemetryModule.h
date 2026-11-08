#pragma once

#include "DebugConfiguration.h"
#include "NodeDB.h"
#include "TelemetryScheduler.h"
#include "configuration.h"
#include "sleep.h"

/// Where each module keeps its schedule across a deep sleep
enum TelemetrySleepSlot : uint8_t {
    TELEMETRY_SLEEP_SLOT_ENVIRONMENT,
    TELEMETRY_SLEEP_SLOT_AIR_QUALITY,
    TELEMETRY_SLEEP_SLOT_POWER,
    TELEMETRY_SLEEP_SLOT_HEALTH,
    TELEMETRY_SLEEP_SLOTS
};

class BaseTelemetryModule
{
  public:
    virtual ~BaseTelemetryModule() { unregisterForSharedSleep(); }

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

    // Deep sleep shared by the modules that read on their own cadence: a power-saving SENSOR sleeps
    // only while every one of them is idle, and wakes for whichever needs it first.

    /// ms until this module next needs the node awake, its sensor warm-up included; UINT32_MAX = never
    virtual uint32_t msUntilNeededAwake() { return UINT32_MAX; }
    /// True while a warm-up, read or publish is under way
    virtual bool isTelemetryBusy() { return false; }
    /// Last chance to keep state across a deep sleep of sleepMs
    virtual void onTelemetryDeepSleep(uint32_t sleepMs) {}
    /// A stamp on this module's stored-read grid, 0 = no grid yet
    virtual uint32_t readGridStampMs() const { return 0; }

    void registerForSharedSleep();
    void unregisterForSharedSleep();

    /// ms the node can deep sleep now, 0 if a registered module is busy or needs it too soon
    static uint32_t sharedSleepBudgetMs();

    /// Deep sleep for the shared budget. Returns only when the budget is too short to be worth it.
    static void enterSharedDeepSleep();

    /// Another module's grid stamp, for a module (re)starting its grid to read in the same wake-up; 0 = none
    static uint32_t sharedReadAnchorMs(const BaseTelemetryModule *self);

    /// A module published to the mesh since boot (each deep sleep ends in a reboot): the NodeDB is saved before sleeping
    static void noteSentToMesh() { sentToMeshThisWake = true; }

    /// From onTelemetryDeepSleep(): keep the schedule for the wake-up
    static void saveScheduleForSleep(TelemetrySleepSlot slot, const TelemetryScheduler &schedule);
    /// At init: true if this boot is the wake-up from a telemetry deep sleep and the schedule carried on
    static bool restoreScheduleAfterSleep(TelemetrySleepSlot slot, TelemetryScheduler &schedule);

  private:
    static constexpr uint8_t MAX_SLEEP_PARTICIPANTS = 4;
    static BaseTelemetryModule *sleepParticipants[MAX_SLEEP_PARTICIPANTS];
    static uint8_t numSleepParticipants;
    static bool sentToMeshThisWake;
};

// Shortest deep sleep worth taking, and the time from a timer wake-up until a module runs again
#ifndef TELEMETRY_MIN_DEEP_SLEEP_MS
#define TELEMETRY_MIN_DEEP_SLEEP_MS (30 * 1000)
#endif
#ifndef TELEMETRY_BOOT_MARGIN_MS
#define TELEMETRY_BOOT_MARGIN_MS (15 * 1000) // measured ~12 s from timer wake-up to module init on ESP32-S3
#endif
