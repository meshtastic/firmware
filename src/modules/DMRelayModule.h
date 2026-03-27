#pragma once

#include "SinglePortModule.h"

class DMRelayModule : public SinglePortModule {
  public:
    DMRelayModule() : SinglePortModule("dm_relay", meshtastic_PortNum_TEXT_MESSAGE_APP) {}

  protected:
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
};

extern DMRelayModule *dmRelayModule;
