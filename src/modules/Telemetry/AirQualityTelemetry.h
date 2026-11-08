#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#pragma once
#include "BaseTelemetryModule.h"
#include <map>
#include <memory>

#ifndef AIR_QUALITY_TELEMETRY_MODULE_ENABLE
#define AIR_QUALITY_TELEMETRY_MODULE_ENABLE 0
#endif

// Readings retained between offloads, ~216 bytes each. Floor is one LoRa frame's worth (~6 fit
// Data.payload); PSRAM boards go past that for free, the ring is heap-allocated.
#ifndef AIR_QUALITY_TELEMETRY_HISTORY_SIZE
#if MESHTASTIC_MEM_CLASS >= MEM_CLASS_LARGE
#define AIR_QUALITY_TELEMETRY_HISTORY_SIZE 64
#elif MESHTASTIC_MEM_CLASS >= MEM_CLASS_MEDIUM
#define AIR_QUALITY_TELEMETRY_HISTORY_SIZE 16
#else
#define AIR_QUALITY_TELEMETRY_HISTORY_SIZE 8
#endif
#endif

// SENSOR role persists readings to AIR_QUALITY_TELEMETRY_HISTORY_PATH (default /prefs/aq_history.bin, see
// FileTelemetryStore.h for the filesystem choice); other roles and a file that cannot be opened use RAM.
#ifndef AIR_QUALITY_TELEMETRY_HISTORY_FS
#define AIR_QUALITY_TELEMETRY_HISTORY_FS FSCom
#endif

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "NodeDB.h"
#include "ProtobufModule.h"
#include "TelemetryScheduler.h"
#include "TelemetryStore.h"
#include "detect/ScanI2C.h"
#include "detect/ScanI2CConsumer.h"
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

class AirQualityTelemetryModule : private concurrency::OSThread,
                                  public ScanI2CConsumer,
                                  public BaseTelemetryModule,
                                  public ProtobufModule<meshtastic_Telemetry>
{
    CallbackObserver<AirQualityTelemetryModule, const meshtastic::Status *> nodeStatusObserver =
        CallbackObserver<AirQualityTelemetryModule, const meshtastic::Status *>(this,
                                                                                &AirQualityTelemetryModule::handleStatusUpdate);

  public:
    AirQualityTelemetryModule()
        : concurrency::OSThread("AirQualityTelemetry"), ScanI2CConsumer(),
          ProtobufModule("AirQualityTelemetry", meshtastic_PortNum_TELEMETRY_APP, &meshtastic_Telemetry_msg)
    {
        lastMeasurementPacket = nullptr;
        nodeStatusObserver.observe(&nodeStatus->onNewStatus);
        setIntervalFromNow(10 * 1000);
        openHistory(); // safe here: fsInit() and setupSDCard() both run before setupModules()
        registerForSharedSleep();
    }

    virtual bool wantUIFrame() override;
#if !HAS_SCREEN
    void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
#else
    virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;
#endif

    /// The mesh publishes what the read cadence stored, on its own interval
    static bool shouldSendToMesh(bool haveUnsentReading, bool meshDue, bool meshAllowed);

  protected:
    /** Called to handle a particular incoming message
    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Telemetry *p) override;
    virtual int32_t runOnce() override;
    /** Called to get current Air Quality data
    @return true if it contains valid data
    */
    bool getAirQualityTelemetry(meshtastic_Telemetry *m);
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
    /// Read every sensor once; store it when on the read cadence, hand it to a connected phone
    void captureReading(uint32_t nowMs, const TelemetryCadence &cadence, bool phoneConnected);
    /// SENSOR role: the readings the mesh has not had yet, as one TelemetryRecordHistory
    bool sendBufferedTelemetry();
    void logTelemetry(const meshtastic_Telemetry &m);
    void rememberForScreen(const meshtastic_Telemetry &m);
    /// Power-saving SENSOR: tell a connected client the node is about to sleep
    void notifySleepAfterSend();
    TelemetryCadence cadence() const;
    uint32_t lastMeshMs() const;

    bool firstTime = true;
    meshtastic_MeshPacket *lastMeasurementPacket;

    // Readings on the read cadence; the mesh publishes these rather than reading again. Never null
    // once openHistory() has run.
    std::unique_ptr<TelemetryStore<meshtastic_AirQualityMetrics>> history;

    TelemetryScheduler schedule;
    meshtastic_Telemetry latest = meshtastic_Telemetry_init_zero; // newest valid read, stored or not
    bool hasLatest = false;
    bool warmingUp = false;
    bool measuring = false; // initialised with sensors to read, so it takes part in the shared sleep

    // Map for supported sensors to re-scan
    std::map<uint8_t, ScanI2C::DeviceType> supportedSensors;
};

#endif
