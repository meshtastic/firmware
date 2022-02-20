#pragma once
#include "GroupPlugin.h"
#include "ProtobufPlugin.h"
#include "concurrency/OSThread.h"

/**
 * Position plugin for sending/receiving positions into the mesh
 */
class GroupPlugin : private concurrency::OSThread, public ProtobufPlugin<GroupInfo>
{
  public:
    GroupPlugin();

  protected:
    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceivedProtobuf(const MeshPacket &mp, GroupInfo *p) override;

    /** Messages can be received that have the want_response bit set.  If set, this callback will be invoked
     * so that subclasses can (optionally) send a response back to the original sender.  */
    virtual MeshPacket *allocReply() override;

    /** Does our periodic broadcast */
    virtual int32_t runOnce() override;
};

extern GroupPlugin *groupPlugin;
