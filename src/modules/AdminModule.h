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

    /// Note an admin request leaving this node for a remote, so that remote's response is
    /// accepted. Called from the client-to-mesh path (MeshService::handleToRadio).
    void noteOutgoingAdminRequest(const meshtastic_MeshPacket &p);

  private:
    // An admin response carries no session passkey and its sender is not an admin-key holder, so
    // the only thing vouching for one is a request we sent. Track those per remote: a client may
    // have several nodes in flight, and each keeps answering until the window lapses.
    static constexpr size_t kOutstandingAdminRequests = 3;
    static constexpr uint32_t kOutstandingAdminRequestSecs = 300; // same window as the session passkey
    struct OutstandingAdminRequest {
        NodeNum to;          // 0 = free slot
        uint32_t sentAtSecs; // millis()/1000 when the request went out
        uint8_t key[32];     // destination key, when the client pinned one
        bool keyValid;
    };
    OutstandingAdminRequest outstandingAdminRequests[kOutstandingAdminRequests] = {};

    /// Whether mp answers a request we actually sent to mp.from.
    bool responseIsSolicited(const meshtastic_MeshPacket &mp);
    void handleStoreDeviceUIConfig(const meshtastic_DeviceUIConfig &uicfg);
    void handleSendInputEvent(const meshtastic_AdminMessage_InputEvent &inputEvent);
    void reboot(int32_t seconds);

    void setPassKey(meshtastic_AdminMessage *res);
    bool checkPassKey(meshtastic_AdminMessage *res);

    bool messageIsResponse(const meshtastic_AdminMessage *r);
    bool messageIsRequest(const meshtastic_AdminMessage *r);
    void sendWarning(const char *format, ...) __attribute__((format(printf, 2, 3)));
    void sendWarningAndLog(const char *format, ...) __attribute__((format(printf, 2, 3)));
    void warnOnLoraPresetChange(const meshtastic_Config_LoRaConfig &oldLora, const meshtastic_Config_LoRaConfig &newLora);
    void warnOnChannelSet(const meshtastic_Channel &cc);

    // Channel-configuration warnings are coalesced into a single client notification.
    // queueChannelWarning() records one warning for a channel; while an edit transaction
    // is open (begin/commit_edit_settings) the warnings accumulate and flushChannelWarnings()
    // emits one combined message at commit. Outside a transaction the caller flushes
    // immediately, so a single channel/preset edit still produces a single message.
    void queueChannelWarning(uint8_t channelIndex, bool nameIssue, bool pskIssue, const char *format, ...)
        __attribute__((format(printf, 5, 6)));
    // Emit the "licensed mode activated" notice, deferring to commit during an edit transaction
    // so repeated triggers (e.g. owner + several channels) produce a single message.
    void warnLicensedMode();
    void flushChannelWarnings();

    char pendingWarningText[250] = {};    // the lone queued message, used verbatim when only one fired
    uint32_t pendingWarningChannels = 0;  // bitmask of channel indices with a queued warning
    uint8_t pendingWarningCount = 0;      // number of queued warnings this transaction
    bool pendingWarningNameIssue = false; // any queued warning was about a channel name
    bool pendingWarningPskIssue = false;  // any queued warning was about a PSK
    bool pendingLicenseWarning = false;   // a licensed-mode notice is queued for this transaction
};

static constexpr const char *licensedModeMessage =
    "Licensed mode activated, removing admin channel and encryption from all channels";

static constexpr const char *publicChannelPrecisionMessage =
    "Precise position is not allowed on a public (open / known-key) channel; reduced to coarse precision";

extern AdminModule *adminModule;

void disableBluetooth();