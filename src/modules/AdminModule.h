#pragma once
#ifdef ESP_PLATFORM
#include <esp_ota_ops.h>
#endif
#include "ProtobufModule.h"
#include <sys/types.h>
#if HAS_WIFI
#include "mesh/wifi/WiFiAPClient.h"
#endif

/**
 * Datatype passed to Observers by AdminModule, to allow external handling of admin messages
 */
struct AdminModule_ObserverData {
    const meshtastic_AdminMessage *request;
    meshtastic_AdminMessage *response;
    AdminMessageHandleResult *result;
};

/**
 * Admin module for admin messages
 */
class AdminModule : public ProtobufModule<meshtastic_AdminMessage>, public Observable<AdminModule_ObserverData *>
{
    friend class AdminModuleTestShim; // test/support/AdminModuleTestShim.h - native tests reach the private handlers/state

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
    void handleGetDeviceUIConfig(const meshtastic_MeshPacket &req);
    /**
     * Setters
     */
    void handleSetOwner(const meshtastic_User &o);
    void handleSetChannel(const meshtastic_Channel &cc);

  protected:
    void handleSetConfig(const meshtastic_Config &c, bool fromOthers);

#ifdef PIO_UNIT_TESTING
  protected:
#else
  private:
#endif
    bool handleSetModuleConfig(const meshtastic_ModuleConfig &c);
    void handleSetChannel();

  public:
    void handleSetHamMode(const meshtastic_HamParameters &req);

  private:
    void handleStoreDeviceUIConfig(const meshtastic_DeviceUIConfig &uicfg);
    void handleSendInputEvent(const meshtastic_AdminMessage_InputEvent &inputEvent);
    void reboot(int32_t seconds);

    void setPassKey(meshtastic_AdminMessage *res);
    bool checkPassKey(meshtastic_AdminMessage *res);

    bool messageIsResponse(const meshtastic_AdminMessage *r);
    bool messageIsRequest(const meshtastic_AdminMessage *r);
    void sendWarning(const char *format, ...) __attribute__((format(printf, 2, 3)));
    void sendWarningAndLog(const char *format, ...) __attribute__((format(printf, 2, 3)));
};

static constexpr const char *licensedModeMessage =
    "Licensed mode activated, removing admin channel and encryption from all channels";

static constexpr const char *publicChannelPrecisionMessage =
    "Precise position is not allowed on a public (open / known-key) channel; reduced to coarse precision";

extern AdminModule *adminModule;

void disableBluetooth();