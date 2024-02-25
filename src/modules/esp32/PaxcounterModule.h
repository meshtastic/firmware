#pragma once

#include "ProtobufModule.h"
#include "configuration.h"
#if defined(ARCH_ESP32)
#include "../mesh/generated/meshtastic/paxcount.pb.h"
#include "NodeDB.h"
#include <libpax_api.h>

/**
 * A simple example module that just replies with "Message received" to any message it receives.
 */
class PaxcounterModule : private concurrency::OSThread, public ProtobufModule<meshtastic_Paxcount>
{
    bool firstTime = true;

  public:
    PaxcounterModule();

  protected:
    struct count_payload_t count_from_libpax = {0, 0, 0};
    virtual int32_t runOnce() override;
    bool sendInfo(NodeNum dest = NODENUM_BROADCAST);
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Paxcount *p) override;
    virtual meshtastic_MeshPacket *allocReply() override;
    bool isActive() { return moduleConfig.paxcounter.enabled && !config.bluetooth.enabled && !config.network.wifi_enabled; }
#if HAS_SCREEN
    virtual bool wantUIFrame() override { return isActive(); }
    virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;
#endif
};

extern PaxcounterModule *paxcounterModule;
#endif