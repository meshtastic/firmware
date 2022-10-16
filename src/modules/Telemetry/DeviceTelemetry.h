#pragma once
#include "../mesh/generated/telemetry.pb.h"
#include "NodeDB.h"
#include "ProtobufModule.h"
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

class DeviceTelemetryModule : private concurrency::OSThread, public ProtobufModule<Telemetry>
{
  public:
    DeviceTelemetryModule()
        : concurrency::OSThread("DeviceTelemetryModule"), ProtobufModule("DeviceTelemetry", PortNum_TELEMETRY_APP, &Telemetry_msg)
    {
      lastMeasurementPacket = nullptr;
      setIntervalFromNow(10 * 1000);
    }
    virtual bool wantUIFrame() { return false; }

  protected:
    /** Called to handle a particular incoming message
    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceivedProtobuf(const MeshPacket &mp, Telemetry *p) override;
    virtual int32_t runOnce() override;
    /**
     * Send our Telemetry into the mesh
     */
    bool sendTelemetry(NodeNum dest = NODENUM_BROADCAST, bool phoneOnly = false);

  private:
    uint32_t sendToPhoneIntervalMs = SECONDS_IN_MINUTE * 1000; // Send to phone every minute
    uint32_t lastSentToMesh = 0;
    const MeshPacket *lastMeasurementPacket;
};
