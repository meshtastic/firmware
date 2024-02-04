#pragma once
#include "ProtobufModule.h"

/**
 * Waypoint message handling for meshtastic
 */
class AtakPluginModule : public ProtobufModule<meshtastic_TAK_Packet>, private concurrency::OSThread
{
  public:
    /** Constructor
     * name is for debugging output
     */
    AtakPluginModule();

  protected:
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_TAK_Packet *t) override;
    virtual void alterReceivedProtobuf(meshtastic_MeshPacket &mp, meshtastic_TAK_Packet *t) override;
    /* Does our periodic broadcast */
    int32_t runOnce() override;
};

extern AtakPluginModule *atakPluginModule;