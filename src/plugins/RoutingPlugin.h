#pragma once
#include "ProtobufPlugin.h"

/**
 * Routing plugin for router control messages
 */
class RoutingPlugin : public ProtobufPlugin<Routing>
{
  public:
    /** Constructor
     * name is for debugging output
     */
    RoutingPlugin();

  protected:
    friend class Router;

    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceivedProtobuf(const MeshPacket &mp, const Routing *p);

    /** Messages can be received that have the want_response bit set.  If set, this callback will be invoked
     * so that subclasses can (optionally) send a response back to the original sender.  */
    virtual MeshPacket *allocReply(); 

    /// Override wantPacket to say we want to see all packets, not just those for our port number
    virtual bool wantPacket(const MeshPacket *p) { return true; }

    void sendAckNak(Routing_Error err, NodeNum to, PacketId idFrom);
};

extern RoutingPlugin *routingPlugin;