#pragma once

#include "SinglePortModule.h"
#include "mesh/generated/meshtastic/portnums.pb.h"

// Plugin example: answers "/ping <payload>" text messages with "pong <payload>".
class PluginEcho : public SinglePortModule
{
  public:
    PluginEcho();

    void setup() override;
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
};
