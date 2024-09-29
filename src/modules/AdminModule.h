#pragma once
#include "ProtobufModule.h"
#if HAS_WIFI
#include "mesh/wifi/WiFiAPClient.h"
#endif

/**
 * Admin module for admin messages
 */
class AdminModule : public ProtobufModule<meshtastic_AdminMessage>, public Observable<const meshtastic_AdminMessage *>
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

    uint8_t session_passkey[8] = {0};
    uint session_time = 0;

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

    void setPassKey(meshtastic_AdminMessage *res);
    bool checkPassKey(meshtastic_AdminMessage *res);

    bool messageIsResponse(const meshtastic_AdminMessage *r);
    bool messageIsRequest(const meshtastic_AdminMessage *r);
};

extern AdminModule *adminModule;

void disableBluetooth();