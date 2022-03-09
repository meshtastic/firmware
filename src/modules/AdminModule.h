#pragma once
#include "ProtobufModule.h"

/**
 * Routing module for router control messages
 */
class AdminModule : public ProtobufModule<AdminMessage>
{
  public:
    /** Constructor
     * name is for debugging output
     */
    AdminModule();

  protected:
    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceivedProtobuf(const MeshPacket &mp, AdminMessage *p) override;

  private:
    void handleSetOwner(const User &o);
    void handleSetChannel(const Channel &cc);
    void handleSetRadio(RadioConfig &r);

    void handleGetChannel(const MeshPacket &req, uint32_t channelIndex);
    void handleGetRadio(const MeshPacket &req);
    void handleGetOwner(const MeshPacket &req);
};

extern AdminModule *adminModule;
