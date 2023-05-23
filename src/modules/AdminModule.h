#pragma once
#include "ProtobufModule.h"
#ifdef ARCH_ESP32
#include "mesh/http/WiFiAPClient.h"
#endif

/**
 * Admin module for admin messages
 */
class AdminModule : public ProtobufModule<meshtastic_AdminMessage>
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
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *p) override;

  private:
    bool hasOpenEditTransaction = false;

    void saveChanges(int saveWhat, bool shouldReboot = true);

    /**
     * Getters
     */
    void handleGetModuleConfigResponse(const meshtastic_MeshPacket &req, meshtastic_AdminMessage *p);
    void handleGetOwner(const meshtastic_MeshPacket &req);
    void handleGetConfig(const meshtastic_MeshPacket &req, uint32_t configType);
    void handleGetModuleConfig(const meshtastic_MeshPacket &req, uint32_t configType);
    void handleGetChannel(const meshtastic_MeshPacket &req, uint32_t channelIndex);
    void handleGetDeviceMetadata(const meshtastic_MeshPacket &req);
    void handleGetDeviceConnectionStatus(const meshtastic_MeshPacket &req);
    void handleGetNodeRemoteHardwarePins(const meshtastic_MeshPacket &req);
    /**
     * Setters
     */
    void handleSetOwner(const meshtastic_User &o);
    void handleSetChannel(const meshtastic_Channel &cc);
    void handleSetConfig(const meshtastic_Config &c);
    void handleSetModuleConfig(const meshtastic_ModuleConfig &c);
    void handleSetChannel();
    void handleSetHamMode(const meshtastic_HamParameters &req);
    void reboot(int32_t seconds);
};

extern AdminModule *adminModule;
