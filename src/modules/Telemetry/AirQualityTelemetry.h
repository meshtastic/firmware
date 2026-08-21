#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#pragma once
#include "BaseTelemetryModule.h"
#include <map>

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

// Define AIR_QUALITY_TELEMETRY_HISTORY_PATH to persist readings instead of keeping them in RAM;
// see the filesystem-choice note in FileTelemetryStore.h. Falls back to RAM if it cannot be opened.
#ifndef AIR_QUALITY_TELEMETRY_HISTORY_FS
#define AIR_QUALITY_TELEMETRY_HISTORY_FS FSCom
#endif

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "NodeDB.h"
#include "ProtobufModule.h"
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
    }

    ~AirQualityTelemetryModule() { delete history; }
    virtual bool wantUIFrame() override;
#if !HAS_SCREEN
    void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
#else
    virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;
#endif

    // Local loop runs at its own cadence, independent of the on-air interval; either consumer can
    // drive it. everRead, not a lastReadMs sentinel, marks the never-read state.
    static bool shouldReadSensors(bool everRead, uint32_t nowMs, uint32_t lastReadMs, uint32_t localIntervalMs,
                                  bool phoneQueueEmpty, bool meshPublishDue);

    // The mesh samples the loop on its own interval. A power-saving SENSOR takes this path even with
    // nothing to send, because sendTelemetry() is what arms its deep sleep.
    static bool shouldSendToMesh(bool haveUnsentReading, bool meshDue, bool meshAllowed, bool powerSavingSensor);

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
    /// Publish the newest reading to the mesh or the phone, unless that destination already had it.
    bool sendTelemetry(NodeNum dest = NODENUM_BROADCAST, bool phoneOnly = false);

    virtual AdminMessageHandleResult handleAdminMessageForModule(const meshtastic_MeshPacket &mp,
                                                                 meshtastic_AdminMessage *request,
                                                                 meshtastic_AdminMessage *response) override;
    void i2cScanFinished(ScanI2C *i2cScanner);

  private:
    void openHistory();
    /// Read every sensor once and push the result into history with the time it was taken.
    void captureReading();
    /// Wake any sleeping sensor. @return ms to wait for the slowest one, 0 if all are ready.
    int32_t warmUpSensors();
    void logTelemetry(const meshtastic_Telemetry &m);

    bool firstTime = true;
    meshtastic_MeshPacket *lastMeasurementPacket;

    // Local device-to-phone cadence and the thread's tick; unrelated to air_quality_interval,
    // which paces what goes on air.
    uint32_t localLoopIntervalMs = SECONDS_IN_MINUTE * 1000;

    // The offload publishes one of these rather than reading again, so one warm-up feeds the phone,
    // the mesh, the screen and on-demand requests. Never null once openHistory() has run.
    TelemetryStore<meshtastic_AirQualityMetrics> *history = nullptr;

    uint32_t lastReadMs = 0; // monotonic millis() of the last read attempt
    bool everRead = false;   // false until the first read attempt, whatever its outcome

    // Map for supported sensors to re-scan
    std::map<uint8_t, ScanI2C::DeviceType> supportedSensors;
};

#endif
