#pragma once

#include "configuration.h"

#if HAS_CELLULAR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "BaseTelemetryModule.h"
#include "NodeDB.h"
#include "ProtobufModule.h"

class CellularTelemetryModule : private concurrency::OSThread,
                                public BaseTelemetryModule,
                                public ProtobufModule<meshtastic_Telemetry>
{
    CallbackObserver<CellularTelemetryModule, const meshtastic::Status *> nodeStatusObserver =
        CallbackObserver<CellularTelemetryModule, const meshtastic::Status *>(this, &CellularTelemetryModule::handleStatusUpdate);

  public:
    CellularTelemetryModule()
        : concurrency::OSThread("CellularTelemetry"),
          ProtobufModule("CellularTelemetry", meshtastic_PortNum_TELEMETRY_APP, &meshtastic_Telemetry_msg)
    {
        nodeStatusObserver.observe(&nodeStatus->onNewStatus);
        setIntervalFromNow(10 * 1000);
    }
    virtual bool wantUIFrame() override { return false; }

  protected:
    // Handles an incoming message; true if handled and other handlers should be skipped.
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Telemetry *p) override;
    virtual int32_t runOnce() override;
    // Fills m with cellular diagnostic telemetry; false if nothing valid is available yet.
    bool getCellularTelemetry(meshtastic_Telemetry *m);
    virtual meshtastic_MeshPacket *allocReply() override;
    bool sendTelemetry(NodeNum dest = NODENUM_BROADCAST, bool phoneOnly = false);

  private:
    uint32_t sendToPhoneIntervalMs = SECONDS_IN_MINUTE * 1000; // Send to phone every minute
    uint32_t lastSentToPhone = 0;
};

#endif
