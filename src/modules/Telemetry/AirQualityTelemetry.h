#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#pragma once
#include "BaseTelemetryModule.h"
#include <map>

#ifndef AIR_QUALITY_TELEMETRY_MODULE_ENABLE
#define AIR_QUALITY_TELEMETRY_MODULE_ENABLE 0
#endif

// Local retention window, shared by mesh/mqtt/phone publishing. Each slot is
// sizeof(BufferedReading<meshtastic_AirQualityMetrics>) ~= 220 bytes
// Boards with RAM to spare can raise it by defining AIR_QUALITY_TELEMETRY_HISTORY_SIZE in their
// own variant.h
#ifndef AIR_QUALITY_TELEMETRY_HISTORY_SIZE
#define AIR_QUALITY_TELEMETRY_HISTORY_SIZE 16
#endif

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "NodeDB.h"
#include "ProtobufModule.h"
#include "TelemetryHistory.h"
#include "detect/ScanI2C.h"
#include "detect/ScanI2CConsumer.h"
#include "mqtt/MQTT.h"
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

  protected:
    /** Called to handle a particular incoming message
    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Telemetry *p) override;
    virtual int32_t runOnce() override;

    bool getAirQualityTelemetry(meshtastic_Telemetry *m);

    virtual meshtastic_MeshPacket *allocReply() override;

    bool sendTelemetry(NodeNum dest = NODENUM_BROADCAST);
    void logSendPacket(meshtastic_Telemetry &m);

    meshtastic_MeshPacket *allocTelemetryHistoryPacket() override { return allocDataPacket(); }
    meshtastic_MeshPacket *allocTelemetryPacket(const meshtastic_Telemetry &m) override { return allocDataProtobuf(m); }

    // Keep our own copy of whatever was just published
    void onPublishedTelemetry(const meshtastic_MeshPacket &p) override
    {
        if (lastMeasurementPacket != nullptr)
            packetPool.release(lastMeasurementPacket);
        lastMeasurementPacket = packetPool.allocCopy(p);
    }

    virtual AdminMessageHandleResult handleAdminMessageForModule(const meshtastic_MeshPacket &mp,
                                                                 meshtastic_AdminMessage *request,
                                                                 meshtastic_AdminMessage *response) override;
    void i2cScanFinished(ScanI2C *i2cScanner);

  private:
    bool firstTime = true;

    int32_t awakeAheadOfTimeMs = 0;
    int32_t startAirQualityTelemetryCycle = 0;

    meshtastic_MeshPacket *lastMeasurementPacket;
    uint32_t sendToPhoneIntervalMs = SECONDS_IN_MINUTE * 1000; // Send to phone every minute

    // Map for supported sensors to re-scan
    std::map<uint8_t, ScanI2C::DeviceType> supportedSensors;

    // Telemetry record history, shared by mesh/mqtt/phone publishing
    TelemetryHistoryBuffer<meshtastic_AirQualityMetrics, AIR_QUALITY_TELEMETRY_HISTORY_SIZE> history;
};

#endif
