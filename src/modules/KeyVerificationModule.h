#pragma once

#include "ProtobufModule.h"
#include "SinglePortModule.h"

enum KeyVerificationState {
    KEY_VERIFICATION_IDLE,
    KEY_VERIFICATION_SENDER_HAS_INITIATED,
    KEY_VERIFICATION_SENDER_AWAITING_NUMBER,
    KEY_VERIFICATION_SENDER_AWAITING_USER,
    KEY_VERIFICATION_RECEIVER_AWAITING_USER,
};

class KeyVerificationModule
    : public SinglePortModule //, public ProtobufModule<meshtastic_KeyVerification> //, private concurrency::OSThread //
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
    bool sendResponse(const meshtastic_MeshPacket &, meshtastic_KeyVerification *);

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

  private:
    uint64_t currentNonce = 0;
    uint32_t currentNonceTimestamp = 0;
    NodeNum currentRemoteNode = 0;
    KeyVerificationState currentstate = KEY_VERIFICATION_IDLE;

    void updateState(); // check the timeouts and maybe reset the state to idle
};