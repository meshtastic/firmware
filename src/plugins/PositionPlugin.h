#pragma once
#include "ProtobufPlugin.h"

/**
 * Position plugin for sending/receiving positions into the mesh
 */
class PositionPlugin : public ProtobufPlugin<Position>
{
  public:
    /** Constructor
     * name is for debugging output
     */
    PositionPlugin() : ProtobufPlugin("position", PortNum_POSITION_APP, Position_fields) {}

    /**
     * Send our position into the mesh
     */
    void sendOurPosition(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);

  protected:

    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceivedProtobuf(const MeshPacket &mp, const Position &p);

    /** Messages can be received that have the want_response bit set.  If set, this callback will be invoked
     * so that subclasses can (optionally) send a response back to the original sender.  */
    virtual MeshPacket *allocReply();
};

extern PositionPlugin *positionPlugin;