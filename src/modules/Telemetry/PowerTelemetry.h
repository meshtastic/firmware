#pragma once
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "NodeDB.h"
#include "ProtobufModule.h"
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

class PowerTelemetryModule : private concurrency::OSThread, public ProtobufModule<meshtastic_Telemetry>
{
  public:
    PowerTelemetryModule()
        : concurrency::OSThread("PowerTelemetryModule"),
          ProtobufModule("PowerTelemetry", meshtastic_PortNum_TELEMETRY_APP, &meshtastic_Telemetry_msg)
    {
        lastMeasurementPacket = nullptr;
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
    /**
     * Send our Telemetry into the mesh
     */
    bool sendTelemetry(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);

  private:
    bool firstTime = 1;
    meshtastic_MeshPacket *lastMeasurementPacket;
    uint32_t sendToPhoneIntervalMs = SECONDS_IN_MINUTE * 1000; // Send to phone every minute
    uint32_t lastSentToMesh = 0;
    uint32_t lastSentToPhone = 0;
    uint32_t sensor_read_error_count = 0;
};
