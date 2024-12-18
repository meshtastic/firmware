#pragma once
#include "ProtobufModule.h"
#include "meshtastic/atak.pb.h"

/**
 * Waypoint message handling for meshtastic
 */
class AtakPluginModule : public ProtobufModule<meshtastic_TAKPacket>, private concurrency::OSThread
{
  public:
    /** Constructor
     * name is for debugging output
     */
    AtakPluginModule();

  protected:
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_TAKPacket *t) override;
    virtual void alterReceivedProtobuf(meshtastic_MeshPacket &mp, meshtastic_TAKPacket *t) override;
    /* Does our periodic broadcast */
    int32_t runOnce() override;

  private:
    meshtastic_TAKPacket cloneTAKPacketData(meshtastic_TAKPacket *t);
};

extern AtakPluginModule *atakPluginModule;