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

    void handleGetCannedMessagePluginPart1(const MeshPacket &req);
    void handleGetCannedMessagePluginPart2(const MeshPacket &req);
    void handleGetCannedMessagePluginPart3(const MeshPacket &req);
    void handleGetCannedMessagePluginPart4(const MeshPacket &req);
    void handleGetCannedMessagePluginPart5(const MeshPacket &req);

    void handleSetCannedMessagePluginPart1(const CannedMessagePluginMessagePart1 &from_msg);
    void handleSetCannedMessagePluginPart2(const CannedMessagePluginMessagePart2 &from_msg);
    void handleSetCannedMessagePluginPart3(const CannedMessagePluginMessagePart3 &from_msg);
    void handleSetCannedMessagePluginPart4(const CannedMessagePluginMessagePart4 &from_msg);
    void handleSetCannedMessagePluginPart5(const CannedMessagePluginMessagePart5 &from_msg);
};

extern AdminPlugin *adminPlugin;
