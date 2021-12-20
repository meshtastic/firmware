#pragma once
#include "ProtobufPlugin.h"

/**
 * Routing plugin for router control messages
 */
class AdminPlugin : public ProtobufPlugin<AdminMessage>
{
  public:
    /** Constructor
     * name is for debugging output
     */
    AdminPlugin();

  protected:
    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceivedProtobuf(const MeshPacket &mp, AdminMessage *p);

  private:
    void handleSetOwner(const User &o);
    void handleSetChannel(const Channel &cc);
    void handleSetRadio(RadioConfig &r);

    void handleGetChannel(const MeshPacket &req, uint32_t channelIndex);
    void handleGetRadio(const MeshPacket &req);
};

extern AdminPlugin *adminPlugin;