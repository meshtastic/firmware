#pragma once
#include "Channels.h"
#include "ProtobufModule.h"

/**
 * Routing module for router control messages
 */
class RoutingModule : public ProtobufModule<meshtastic_Routing>
{
  public:
    /** Constructor
     * name is for debugging output
     */
    RoutingModule();

    void sendAckNak(meshtastic_Routing_Error err, NodeNum to, PacketId idFrom, ChannelIndex chIndex);

  protected:
    friend class Router;

    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Routing *p) override;

    /** Messages can be received that have the want_response bit set.  If set, this callback will be invoked
     * so that subclasses can (optionally) send a response back to the original sender.  */
    virtual meshtastic_MeshPacket *allocReply() override;

    /// Override wantPacket to say we want to see all packets, not just those for our port number
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override { return true; }
};

extern RoutingModule *routingModule;
