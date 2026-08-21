#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#pragma once
#include "BaseTelemetryModule.h"
#include <map>

#ifndef AIR_QUALITY_TELEMETRY_MODULE_ENABLE
#define AIR_QUALITY_TELEMETRY_MODULE_ENABLE 0
#endif

// How many local-loop readings to retain between offloads. Sized to roughly one LoRa frame's worth:
// a populated AirQualityMetrics encodes to ~35 bytes inside a TelemetryRecord, so about six fit the
// 233-byte Data.payload limit. Each slot costs sizeof(TelemetryReading<meshtastic_AirQualityMetrics>),
// ~216 bytes; a variant short on RAM can lower this, one with room to spare can raise it.
#ifndef AIR_QUALITY_TELEMETRY_HISTORY_SIZE
#define AIR_QUALITY_TELEMETRY_HISTORY_SIZE 8
#endif

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "NodeDB.h"
#include "ProtobufModule.h"
#include "TelemetryHistory.h"
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
    }
    virtual bool wantUIFrame() override;
#if !HAS_SCREEN
    void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
#else
    virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;
#endif

    // Pure local-loop policy: the device reads at its own near-realtime cadence so an attached phone
    // sees live values, independent of the on-air interval. Either consumer can drive the read - the
    // offload keeps the loop alive when a backed-up phone queue would otherwise stall it. everRead
    // (rather than a lastReadMs sentinel) marks the never-read state, so a read stamped at
    // millis()==0 still honors the cadence.
    static bool shouldReadSensors(bool everRead, uint32_t nowMs, uint32_t lastReadMs, uint32_t localIntervalMs,
                                  bool phoneQueueEmpty, bool meshPublishDue);

    // Pure offload policy: the mesh takes a sample of the local loop's readings on its own interval,
    // not every one of them. A power-saving SENSOR takes this path even with nothing left to send,
    // because sendTelemetry() is also what arms its deep sleep.
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
    /**
     * Publish the newest reading in history to the mesh or the phone. Does nothing if nothing has
     * been read yet, or if this destination already had that reading.
     * @return true if a packet was handed off to send
     */
    bool sendTelemetry(NodeNum dest = NODENUM_BROADCAST, bool phoneOnly = false);

    virtual AdminMessageHandleResult handleAdminMessageForModule(const meshtastic_MeshPacket &mp,
                                                                 meshtastic_AdminMessage *request,
                                                                 meshtastic_AdminMessage *response) override;
    void i2cScanFinished(ScanI2C *i2cScanner);

  private:
    /// Read every sensor once and push the result into history with the time it was taken.
    void captureReading();
    /// Wake any sleeping sensor. @return ms to wait for the slowest one, 0 if all are ready.
    int32_t warmUpSensors();
    void logTelemetry(const meshtastic_Telemetry &m);

    bool firstTime = true;
    meshtastic_MeshPacket *lastMeasurementPacket;

    // Cadence of the local device-to-phone loop, and the thread's own tick. Deliberately unrelated
    // to air_quality_interval: that one paces what goes on air.
    uint32_t localLoopIntervalMs = SECONDS_IN_MINUTE * 1000;

    // What the local loop has measured, newest last. The offload publishes one of these readings
    // rather than triggering a read of its own, so one sensor warm-up feeds the phone, the mesh, the
    // screen and on-demand requests, and a packet is never restamped as fresher than it is.
    TelemetryHistory<meshtastic_AirQualityMetrics, AIR_QUALITY_TELEMETRY_HISTORY_SIZE> history;

    uint32_t lastReadMs = 0; // monotonic millis() of the last read attempt
    bool everRead = false;   // false until the first read attempt, whatever its outcome

    // Map for supported sensors to re-scan
    std::map<uint8_t, ScanI2C::DeviceType> supportedSensors;
};

#endif
