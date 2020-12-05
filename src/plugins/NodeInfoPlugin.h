#pragma once
#include "ProtobufPlugin.h"

/**
 * NodeInfo plugin for sending/receiving NodeInfos into the mesh
 */
class NodeInfoPlugin : public ProtobufPlugin<User>
{
  public:
    /** Constructor
     * name is for debugging output
     */
    NodeInfoPlugin() : ProtobufPlugin("nodeinfo", PortNum_NODEINFO_APP, User_fields) {}

    /**
     * Send our NodeInfo into the mesh
     */
    void sendOurNodeInfo(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);

  protected:

    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceivedProtobuf(const MeshPacket &mp, const User &p);
};

extern NodeInfoPlugin nodeInfoPlugin;