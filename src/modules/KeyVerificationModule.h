#pragma once

#include "ProtobufModule.h"
#include "SinglePortModule.h"

enum KeyVerificationState {
    KEY_VERIFICATION_IDLE,
    KEY_VERIFICATION_SENDER_HAS_INITIATED,
    KEY_VERIFICATION_SENDER_AWAITING_NUMBER,
    KEY_VERIFICATION_SENDER_AWAITING_USER,
    KEY_VERIFICATION_RECEIVER_AWAITING_USER,
    KEY_VERIFICATION_RECEIVER_AWAITING_HASH1,
};

class KeyVerificationModule : public ProtobufModule<meshtastic_KeyVerification> //, private concurrency::OSThread //
{
    // CallbackObserver<KeyVerificationModule, const meshtastic::Status *> nodeStatusObserver =
    //     CallbackObserver<KeyVerificationModule, const meshtastic::Status *>(this, &KeyVerificationModule::handleStatusUpdate);

  public:
    KeyVerificationModule();
    /*    : concurrency::OSThread("KeyVerification"),
          ProtobufModule("KeyVerification", meshtastic_PortNum_KEY_VERIFICATION_APP, &meshtastic_KeyVerification_msg)
    {
        nodeStatusObserver.observe(&nodeStatus->onNewStatus);
        setIntervalFromNow(setStartDelay()); // Wait until NodeInfo is sent
    }*/
    virtual bool wantUIFrame() { return false; };
    bool sendInitialRequest(NodeNum remoteNode);
    void generateVerificationCode(char *); // fills char with the user readable verification code
    uint32_t getCurrentRemoteNode() { return currentRemoteNode; }

  protected:
    /* Called to handle a particular incoming message
    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_KeyVerification *p);
    // virtual meshtastic_MeshPacket *allocReply() override;

    // rather than add to the craziness that is the admin module, just handle those requests here.
    virtual AdminMessageHandleResult handleAdminMessageForModule(const meshtastic_MeshPacket &mp,
                                                                 meshtastic_AdminMessage *request,
                                                                 meshtastic_AdminMessage *response) override;
    /*
     * Send our Telemetry into the mesh
     */
    bool sendMetrics();
    virtual meshtastic_MeshPacket *allocReply() override;

  private:
    uint64_t currentNonce = 0;
    uint32_t currentNonceTimestamp = 0;
    NodeNum currentRemoteNode = 0;
    uint32_t currentSecurityNumber = 0;
    KeyVerificationState currentState = KEY_VERIFICATION_IDLE;
    uint8_t hash1[32] = {0}; //
    uint8_t hash2[32] = {0}; //
    char message[40] = {0};

    void processSecurityNumber(uint32_t);
    void updateState(); // check the timeouts and maybe reset the state to idle
    void resetToIdle(); // Zero out module state
};

extern KeyVerificationModule *keyVerificationModule;