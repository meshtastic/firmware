#include "mesh-pb-constants.h"
#include "sharedMem/PacketServer.h"

/**
 * @brief This is a wrapper class for the PaketServer to avoid dealing with DataPackets
 *        in the application code.
 *
 */
class MeshPacketServer : public PacketServer
{
  public:
    MeshPacketServer();
    static void init(void);
    virtual void begin(void);
    virtual bool receivePacket(meshtastic_ToRadio &to);
    virtual bool sendPacket(meshtastic_FromRadio &from);
    virtual bool sendPacket(meshtastic_FromRadio &&from);
};

extern MeshPacketServer *meshPacketServer;