#pragma once
#include "MeshPlugin.h"

/**
 * NodeInfo plugin for sending/receiving NodeInfos into the mesh
 */
class MQTTPlugin : public MeshPlugin
{
  public:
    MQTTPlugin();

  protected:
    /** We sniff all packets */
    virtual bool handleReceived(const MeshPacket &mp);

    virtual bool wantPacket(const MeshPacket *p) { return true; }
};
