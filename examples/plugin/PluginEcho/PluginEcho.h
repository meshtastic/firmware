#pragma once

#include "SinglePortModule.h"
#include "mesh/generated/meshtastic/portnums.pb.h"

// Plugin example: echoes PRIVATE_APP direct messages back to the sender.
class PluginEcho : public SinglePortModule
{
  public:
    PluginEcho();

    void setup() override;
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
};
