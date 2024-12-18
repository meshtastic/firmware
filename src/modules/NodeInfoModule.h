#pragma once
#include "ProtobufModule.h"

/**
 * NodeInfo module for sending/receiving NodeInfos into the mesh
 */
class NodeInfoModule : public ProtobufModule<meshtastic_User>, private concurrency::OSThread
{
    /// The id of the last packet we sent, to allow us to cancel it if we make something fresher
    PacketId prevPacketId = 0;

    uint32_t currentGeneration = 0;

  public:
    /** Constructor
     * name is for debugging output
     */
    NodeInfoModule();

    /**
     * Send our NodeInfo into the mesh
     */
    void sendOurNodeInfo(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false, uint8_t channel = 0,
                         bool _shorterTimeout = false);

  protected:
    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_User *p) override;

    /** Messages can be received that have the want_response bit set.  If set, this callback will be invoked
     * so that subclasses can (optionally) send a response back to the original sender.  */
    virtual meshtastic_MeshPacket *allocReply() override;

    /** Does our periodic broadcast */
    virtual int32_t runOnce() override;

  private:
    uint32_t lastSentToMesh = 0; // Last time we sent our NodeInfo to the mesh
    bool shorterTimeout = false;
};

extern NodeInfoModule *nodeInfoModule;
