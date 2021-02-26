#pragma once
#include "ProtobufPlugin.h"

/**
 * Routing plugin for router control messages
 */
class AdminPlugin : public ProtobufPlugin<AdminMessage>
{
  MeshPacket *reply = NULL;
  
  public:
    /** Constructor
     * name is for debugging output
     */
    AdminPlugin();

  protected:
    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceivedProtobuf(const MeshPacket &mp, const AdminMessage *p);

    /** Messages can be received that have the want_response bit set.  If set, this callback will be invoked
     * so that subclasses can (optionally) send a response back to the original sender.  */
    virtual MeshPacket *allocReply();

  private:
    void handleSetOwner(const User &o);
    void handleSetChannel(const Channel &cc);
    void handleSetRadio(const RadioConfig &r);

    void handleGetChannel(const MeshPacket &req, uint32_t channelIndex);
    void handleGetRadio(const MeshPacket &req);
};

extern AdminPlugin *adminPlugin;