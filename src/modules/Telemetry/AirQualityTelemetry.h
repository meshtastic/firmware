#pragma once
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "Adafruit_PM25AQI.h"
#include "NodeDB.h"
#include "ProtobufModule.h"

class AirQualityTelemetryModule : private concurrency::OSThread, public ProtobufModule<meshtastic_Telemetry>
{
  public:
    AirQualityTelemetryModule()
        : concurrency::OSThread("AirQualityTelemetryModule"),
          ProtobufModule("AirQualityTelemetry", meshtastic_PortNum_TELEMETRY_APP, &meshtastic_Telemetry_msg)
    {
        lastMeasurementPacket = nullptr;
        setIntervalFromNow(10 * 1000);
        aqi = Adafruit_PM25AQI();
    }

  protected:
    /** Called to handle a particular incoming message
    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Telemetry *p) override;
    virtual int32_t runOnce() override;
    /**
     * Send our Telemetry into the mesh
     */
    bool sendTelemetry(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);

  private:
    Adafruit_PM25AQI aqi;
    PM25_AQI_Data data = {0};
    bool firstTime = 1;
    meshtastic_MeshPacket *lastMeasurementPacket;
    uint32_t sendToPhoneIntervalMs = SECONDS_IN_MINUTE * 1000; // Send to phone every minute
    uint32_t lastSentToMesh = 0;
};
