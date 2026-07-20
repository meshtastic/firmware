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

// KeyVerification Module overview
// This module allows for two useful functions. First, it implements a 2sv process that can manually verify a trustworthy
// connection with another node. It specifically verifies that the other node holds the correct private key for its public key, so
// it is resistant to MitM attacks. Second, it can be used to bootstrap trust in a new node by carrying the public key in the
// initial unencrypted message (in the hash1 field of the KeyVerification protobuf). This allows a user to manually verify a new
// node even if they don't have that node in the local nodeDB at all.

// The handshake process is as follows (NodeA = initiator, NodeB = responder):
// 1. NodeA sends a KeyVerification message containing a random nonce and its own public key (in the
//    hash1 field) to NodeB. Implemented in sendInitialRequest(). It is PKI-encrypted if NodeA already
//    holds NodeB's key, otherwise channel-encrypted (the bootstrap case).
//
// 2. NodeB replies (allocReply()) with its own public key (hash1 field) and hash2. NodeB generates a
//    random 6-digit security number and stashes NodeA's public key (as a pending key if not already in
//    the nodeDB). It computes hash1 = SHA256(securityNumber, nonce, NodeA_num, NodeB_num, PK_A, PK_B),
//    then hash2 = SHA256(nonce, hash1). The reply is PKI-encrypted only if NodeB already held NodeA's
//    key; in the bootstrap case it is channel-encrypted so NodeA can read NodeB's key from hash1.
//
// 3. NodeA receives the reply (handleReceivedProtobuf()), checks the nonce, stashes NodeB's public key,
//    and prompts the user to enter the security number. The security number is never sent over the mesh
//    and must be communicated over a secondary channel. processSecurityNumber() recomputes hash1 from
//    the entered number and verifies SHA256(nonce, hash1) matches the received hash2. NodeA then sends
//    its hash1 back to NodeB in a PKI-encrypted KeyVerification message (the follow-on PKI packet) and
//    shows the KeyVerificationFinalPrompt menu, displaying 8 characters derived from hash1.
//
// 4. NodeB receives NodeA's hash1 (handleReceivedProtobuf(); required to be PKI-encrypted), checks it
//    matches the hash1 NodeB generated, and shows the same 8-character code for final confirmation.
//
// The final on-screen code comparison is the actual manual verification: the user confirms the codes
// match on both devices, proving the two nodes agree on the same public keys (no MitM substitution).
// PKI-encrypting the follow-on packet additionally proves each node holds the private key for the
// agreed public key.

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
    void commitVerifiedRemoteNode(); // Commit a pending key to NodeDB and mark the node manually verified

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
    void updateState(bool resetTimer = true); // check the timeouts and maybe reset the state to idle
    void resetToIdle();                       // Zero out module state
};

extern KeyVerificationModule *keyVerificationModule;