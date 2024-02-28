#pragma once

#include "mesh-pb-constants.h"
#include "sharedMem/PacketClient.h"

/**
 * @brief This is a wrapper class for the PaketClient to avoid dealing with DataPackets
 *        in the application code.
 *
 */
class MeshPacketClient : public PacketClient
{
  public:
    MeshPacketClient();
    virtual void init(void);
    virtual bool connect(void);
    virtual bool disconnect(void);
    virtual bool isConnected(void);

    virtual bool send(meshtastic_ToRadio &&to);
    virtual meshtastic_FromRadio receive(void);
};
