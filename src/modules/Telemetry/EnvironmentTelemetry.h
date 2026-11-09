#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#pragma once

#include "BaseTelemetryModule.h"
#include <memory>

#ifndef ENVIRONMENTAL_TELEMETRY_MODULE_ENABLE
#define ENVIRONMENTAL_TELEMETRY_MODULE_ENABLE 0
#endif

// Readings retained between mesh publishes, ~240 bytes each, heap-allocated
#ifndef ENVIRONMENT_TELEMETRY_HISTORY_SIZE
#if MESHTASTIC_MEM_CLASS >= MEM_CLASS_LARGE
#define ENVIRONMENT_TELEMETRY_HISTORY_SIZE 64
#elif MESHTASTIC_MEM_CLASS >= MEM_CLASS_MEDIUM
#define ENVIRONMENT_TELEMETRY_HISTORY_SIZE 16
#else
#define ENVIRONMENT_TELEMETRY_HISTORY_SIZE 8
#endif
#endif

// SENSOR role persists readings to ENVIRONMENT_TELEMETRY_HISTORY_PATH (default /prefs/env_history.bin), else RAM
#ifndef ENVIRONMENT_TELEMETRY_HISTORY_FS
#define ENVIRONMENT_TELEMETRY_HISTORY_FS FSCom
#endif

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "NodeDB.h"
#include "ProtobufModule.h"
#include "TelemetryScheduler.h"
#include "TelemetryStore.h"
#include "detect/ScanI2CConsumer.h"
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

class EnvironmentTelemetryModule;
extern EnvironmentTelemetryModule *environmentTelemetryModule;

class EnvironmentTelemetryModule : private concurrency::OSThread,
                                   public ScanI2CConsumer,
                                   public BaseTelemetryModule,
                                   public ProtobufModule<meshtastic_Telemetry>
{
    CallbackObserver<EnvironmentTelemetryModule, const meshtastic::Status *> nodeStatusObserver =
        CallbackObserver<EnvironmentTelemetryModule, const meshtastic::Status *>(this,
                                                                                 &EnvironmentTelemetryModule::handleStatusUpdate);

  public:
    enum class DisplaySource : uint8_t {
        LocalSensor = 0,
        Mesh = 1,
        FavoriteNodesOnly = 2,
    };

    EnvironmentTelemetryModule()
        : concurrency::OSThread("EnvironmentTelemetry"), ScanI2CConsumer(),
          ProtobufModule("EnvironmentTelemetry", meshtastic_PortNum_TELEMETRY_APP, &meshtastic_Telemetry_msg)
    {
        environmentTelemetryModule = this;
        (void)getDisplaySource();
        lastMeasurementPacket = nullptr;
        nodeStatusObserver.observe(&nodeStatus->onNewStatus);
        setIntervalFromNow(10 * 1000);
        openHistory(); // safe here: fsInit() and setupSDCard() both run before setupModules()
        registerForSharedSleep();
    }
    virtual bool wantUIFrame() override;
    static DisplaySource getDisplaySource();
    static void setDisplaySource(DisplaySource source);
    bool ownsFrame(const MeshModule *module) const { return module == this; }
#if !HAS_SCREEN
    void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
#else
    virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;
#endif

    /** Bypass the normal broadcast throttle once, for a sensor with a noteworthy event to
     * report sooner than the next scheduled send (airtime limits still apply). */
    void requestImmediateSend()
    {
        immediateSendRequested = true;
        immediateReadPending = true;
        immediateSendRequestedAtMs = millis();
    }

  protected:
    /** Called to handle a particular incoming message
    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Telemetry *p) override;
    virtual int32_t runOnce() override;
    /** Called to get current Environment telemetry data
    @return true if it contains valid data
    */
    bool getEnvironmentTelemetry(meshtastic_Telemetry *m);
    virtual meshtastic_MeshPacket *allocReply() override;
    /// Phone: the newest read. Mesh: the newest stored reading, unless the mesh already had it.
    bool sendTelemetry(NodeNum dest = NODENUM_BROADCAST, bool phoneOnly = false);

    uint32_t msUntilNeededAwake() override;
    bool isTelemetryBusy() override;
    void onTelemetryDeepSleep(uint32_t sleepMs) override;
    uint32_t readGridStampMs() const override { return measuring ? schedule.lastStoredMs : 0; }

    virtual AdminMessageHandleResult handleAdminMessageForModule(const meshtastic_MeshPacket &mp,
                                                                 meshtastic_AdminMessage *request,
                                                                 meshtastic_AdminMessage *response) override;

    void i2cScanFinished(ScanI2C *i2cScanner);

  private:
    void openHistory();
    /// Stop for good, and stop counting in the shared deep sleep
    int32_t disableTelemetry()
    {
        measuring = false;
        unregisterForSharedSleep();
        return disable();
    }
    /// Read every sensor once; store it when on the read cadence, hand it to a connected phone
    void captureReading(uint32_t nowMs, const TelemetryCadence &cadence, bool phoneConnected);
    /// SENSOR role: the readings the mesh has not had yet, as one TelemetryRecordHistory
    bool sendBufferedTelemetry();
    void logTelemetry(const meshtastic_Telemetry &m);
    void notifySleepAfterSend();
    TelemetryCadence cadence() const;
    uint32_t lastMeshMs() const;
    void clearMeasurementPacket();
    bool refreshLocalMeasurementPacket();
    void refreshDisplayedMeasurement();
    bool shouldDisplayLocalMeasurement() const;
    bool shouldKeepCurrentRemoteDisplay() const;
    bool shouldDisplayRemoteNode(NodeNum nodeNum) const;

    bool firstTime = 1;
    bool immediateSendRequested = false;
    bool immediateReadPending = false; // the request takes one fresh stored reading, not one per poll
    uint32_t immediateSendRequestedAtMs = 0;
    meshtastic_MeshPacket *lastMeasurementPacket;
    uint32_t lastLocalDisplayRefreshMs = 0;

    // Readings on the read cadence; the mesh publishes these rather than reading again. Never null
    // once openHistory() has run.
    std::unique_ptr<TelemetryStore<meshtastic_EnvironmentMetrics>> history;
    TelemetryScheduler schedule;
    meshtastic_Telemetry latest = meshtastic_Telemetry_init_zero; // newest valid read, stored or not
    bool hasLatest = false;
    bool warmingUp = false;
    bool measuring = false; // initialised with sensors to read, so it takes part in the shared sleep
};

#endif
