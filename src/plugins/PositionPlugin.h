#pragma once
#include "ProtobufPlugin.h"
#include "concurrency/OSThread.h"

/**
 * Position plugin for sending/receiving positions into the mesh
 */
class PositionPlugin : public ProtobufPlugin<Position>, private concurrency::OSThread
{
    /// The id of the last packet we sent, to allow us to cancel it if we make something fresher
    PacketId prevPacketId = 0;

    /// We limit our GPS broadcasts to a max rate
    uint32_t lastGpsSend = 0;

    /// We force a rebroadcast if the radio settings change
    uint32_t currentGeneration = 0;

  public:
    /** Constructor
     * name is for debugging output
     */
    PositionPlugin();
    
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

    /** Does our periodic broadcast */
    virtual int32_t runOnce();  
};

extern PositionPlugin *positionPlugin;