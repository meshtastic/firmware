#pragma once
#include "ProtobufModule.h"

/**
 * Admin module for admin messages
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
    bool hasOpenEditTransaction = false;

    void saveChanges(int saveWhat, bool shouldReboot = true);
    /**
     * Getters
     */
    void handleGetOwner(const MeshPacket &req);
    void handleGetConfig(const MeshPacket &req, uint32_t configType);
    void handleGetModuleConfig(const MeshPacket &req, uint32_t configType);
    void handleGetChannel(const MeshPacket &req, uint32_t channelIndex);
    void handleGetDeviceMetadata(const MeshPacket &req);

    /**
     * Setters
     */
    void handleSetOwner(const User &o);
    void handleSetChannel(const Channel &cc);
    void handleSetConfig(const Config &c);
    void handleSetModuleConfig(const ModuleConfig &c);
    void handleSetChannel();
    void reboot(int32_t seconds);
};

extern AdminModule *adminModule;
