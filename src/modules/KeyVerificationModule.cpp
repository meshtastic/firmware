#include "KeyVerificationModule.h"
#include "MeshService.h"
#include "RTC.h"
#include "main.h"
#include "modules/AdminModule.h"
#include <SHA256.h>

KeyVerificationModule *keyVerificationModule;

KeyVerificationModule::KeyVerificationModule()
    : ProtobufModule("KeyVerification", meshtastic_PortNum_KEY_VERIFICATION_APP, &meshtastic_KeyVerification_msg)
{
    ourPortNum = meshtastic_PortNum_KEY_VERIFICATION_APP;
}

AdminMessageHandleResult KeyVerificationModule::handleAdminMessageForModule(const meshtastic_MeshPacket &mp,
                                                                            meshtastic_AdminMessage *request,
                                                                            meshtastic_AdminMessage *response)
{
    updateState();
    if (request->which_payload_variant == meshtastic_AdminMessage_key_verification_admin_tag && mp.from == 0) {
        LOG_WARN("Handling Key Verification Admin Message type %u", request->key_verification_admin.message_type);

        if (request->key_verification_admin.message_type == meshtastic_KeyVerificationAdmin_MessageType_INITIATE_VERIFICATION &&
            currentState == KEY_VERIFICATION_IDLE) {
            sendInitialRequest(request->key_verification_admin.remote_nodenum);

        } else if (request->key_verification_admin.message_type ==
                       meshtastic_KeyVerificationAdmin_MessageType_PROVIDE_SECURITY_NUMBER &&
                   request->key_verification_admin.has_security_number &&
                   currentState == KEY_VERIFICATION_SENDER_AWAITING_NUMBER &&
                   request->key_verification_admin.nonce == currentNonce) { // also check nonce and has_security_number
            processSecurityNumber(request->key_verification_admin.security_number);

        } else if (request->key_verification_admin.message_type == meshtastic_KeyVerificationAdmin_MessageType_DO_VERIFY) {
            resetToIdle();

        } else if (request->key_verification_admin.message_type == meshtastic_KeyVerificationAdmin_MessageType_DO_NOT_VERIFY) {
            resetToIdle();
        }

        // meshtastic_MeshPacket *p = allocDataPacket();
        //  check current state, do rate limiting.

        return AdminMessageHandleResult::HANDLED;
    }
    return AdminMessageHandleResult::NOT_HANDLED;
}

// handle messages to this port

bool KeyVerificationModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_KeyVerification *r)
{
    LOG_WARN("incoming nonce: %u, hash1size: %u hash2size %u", r->nonce, r->hash1.size, r->hash2.size);
    updateState();
    if (mp.pki_encrypted == false)
        return false;
    if (mp.from != currentRemoteNode)
        return false;
    if (currentState == KEY_VERIFICATION_IDLE) {
        return false; // if we're idle, the only acceptable message is an init, which should be handled by allocReply()?

    } else if (currentState == KEY_VERIFICATION_SENDER_HAS_INITIATED && r->nonce == currentNonce && r->hash2.size == 32 &&
               r->hash1.size == 0) {
        memcpy(hash2, r->hash2.bytes, 32);
        if (screen)
            screen->startAlert("Security Number?");
        LOG_WARN("Received hash2, prompting for security number");
        // do sanity checks, store hash2, prompt the user for the security number
        currentState = KEY_VERIFICATION_SENDER_AWAITING_NUMBER;
        return true;

    } else if (currentState == KEY_VERIFICATION_RECEIVER_AWAITING_HASH1 && r->hash1.size == 32) {
        if (memcmp(hash1, r->hash1.bytes, 32) == 0) {
            char verificationCode[9] = {0};
            for (int i = 0; i < 8; i++) {
                // drop the two highest significance bits, then encode as a base64
                verificationCode[i] =
                    (hash1[i] >> 2) + 48; // not a standardized base64, but workable and avoids having a dictionary.
            }
            snprintf(message, 25, "Verification: %s", verificationCode);
            LOG_WARN("Hash1 received");
            if (screen)
                screen->endAlert();
            screen->startAlert(message);
            currentState = KEY_VERIFICATION_RECEIVER_AWAITING_USER;
            return true;
        }
        // do sanity checks, compare the incoming hash1, and prompt the user to accept
    }
    // for each incoming message, do the state timeout check
    // then if the state is not idle, sanity check for the same nonce and the right current state for the received message
    //

    // if this is the response containing hash2:
    // save the message details and prompt the user for the 4 digit code
    //            service->sendToPhone(decompressedCopy);
    // if (screen)
    // screen.showalert();

    // if this is the final message containing hash1:
    // save the details and prompt the user for the final decision
    return false;
}

bool KeyVerificationModule::sendInitialRequest(NodeNum remoteNode)
{
    LOG_WARN("Attempting keyVerification start");
    // generate nonce
    updateState();
    if (currentState != KEY_VERIFICATION_IDLE) {
        return false;
    }
    currentNonce = random();
    currentNonceTimestamp = getTime();
    currentRemoteNode = remoteNode;
    meshtastic_KeyVerification KeyVerification = meshtastic_KeyVerification_init_zero;
    KeyVerification.nonce = currentNonce;
    KeyVerification.hash2.size = 0;
    KeyVerification.hash1.size = 0;
    meshtastic_MeshPacket *p = allocDataProtobuf(KeyVerification);
    p->to = remoteNode;
    p->channel = 0;
    p->pki_encrypted = true;
    p->decoded.want_response = true;
    p->priority = meshtastic_MeshPacket_Priority_HIGH;
    service->sendToMesh(p, RX_SRC_LOCAL, true);
    currentState = KEY_VERIFICATION_SENDER_HAS_INITIATED;
    return true;
}

meshtastic_MeshPacket *KeyVerificationModule::allocReply()
{
    SHA256 hash;
    NodeNum ourNodeNum = nodeDB->getNodeNum();
    updateState();
    if (currentState != KEY_VERIFICATION_IDLE) {
        LOG_WARN("Key Verification requested, but already in a request");
        return nullptr;
    } else if (!currentRequest->pki_encrypted) {
        LOG_WARN("Key Verification requested, but not in a PKI packet");
        return nullptr;
    }
    currentState = KEY_VERIFICATION_RECEIVER_AWAITING_HASH1;
    // There needs to be a cool down period, to prevent this being spammed.
    // Packets coming too fast are just ignored
    // This will be specifically for responding to an initial request packet, as that's the only one with want_response marked
    // true
    auto req = *currentRequest;
    const auto &p = req.decoded;
    meshtastic_KeyVerification scratch;
    meshtastic_KeyVerification response;
    meshtastic_MeshPacket *responsePacket = nullptr;
    pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_KeyVerification_msg, &scratch);

    // check that we were pki encrypted and sent to us

    currentNonce = scratch.nonce;
    response.nonce = scratch.nonce;
    currentRemoteNode = req.from;
    currentNonceTimestamp = getTime();
    currentSecurityNumber = random(1, 999999);

    // generate hash1
    hash.reset();
    hash.update(&currentSecurityNumber, sizeof(currentSecurityNumber));
    hash.update(&currentNonce, sizeof(currentNonce));
    hash.update(&currentRemoteNode, sizeof(currentRemoteNode));
    hash.update(&ourNodeNum, sizeof(ourNodeNum));
    hash.update(currentRequest->public_key.bytes, currentRequest->public_key.size);
    hash.update(owner.public_key.bytes, owner.public_key.size);
    hash.finalize(hash1, 32);

    // generate hash2
    hash.reset();
    hash.update(&currentNonce, sizeof(currentNonce));
    hash.update(hash1, 32);
    hash.finalize(hash2, 32);
    response.hash1.size = 0;
    response.hash2.size = 32;
    memcpy(response.hash2.bytes, hash2, 32);

    responsePacket = allocDataProtobuf(response);

    responsePacket->pki_encrypted = true;
    if (screen) {
        snprintf(message, 25, "Security Number %03u %03u", currentSecurityNumber / 1000, currentSecurityNumber % 1000);
        screen->startAlert(message);
        // screen->startAlert("Security Number 8088");
        LOG_WARN("%s", message);
    }
    LOG_WARN("Security Number %04u", currentSecurityNumber);

    return responsePacket;
}

void KeyVerificationModule::processSecurityNumber(uint32_t incomingNumber)
{
    SHA256 hash;
    NodeNum ourNodeNum = nodeDB->getNodeNum();
    uint8_t scratch_hash[32] = {0};
    LOG_WARN("received security number: %u", incomingNumber);
    meshtastic_NodeInfoLite *remoteNodePtr = nullptr;
    remoteNodePtr = nodeDB->getMeshNode(currentRemoteNode);
    if (remoteNodePtr == nullptr || !remoteNodePtr->has_user || remoteNodePtr->user.public_key.size != 32) {
        currentState = KEY_VERIFICATION_IDLE;
        return; // should we throw an error here?
    }
    LOG_WARN("hashing ");
    // calculate hash1
    hash.reset();
    hash.update(&incomingNumber, sizeof(incomingNumber));
    hash.update(&currentNonce, sizeof(currentNonce));
    hash.update(&ourNodeNum, sizeof(ourNodeNum));
    hash.update(&currentRemoteNode, sizeof(currentRemoteNode));
    hash.update(owner.public_key.bytes, owner.public_key.size);

    hash.update(remoteNodePtr->user.public_key.bytes, remoteNodePtr->user.public_key.size);
    hash.finalize(hash1, 32);

    hash.reset();
    hash.update(&currentNonce, sizeof(currentNonce));
    hash.update(hash1, 32);
    hash.finalize(scratch_hash, 32);

    if (memcmp(scratch_hash, hash2, 32) != 0) {
        LOG_WARN("Hash2 did not match");
        return; // should probably throw an error of some sort
    }
    currentSecurityNumber = incomingNumber;

    meshtastic_KeyVerification KeyVerification = meshtastic_KeyVerification_init_zero;
    KeyVerification.nonce = currentNonce;
    KeyVerification.hash2.size = 0;
    KeyVerification.hash1.size = 32;
    memcpy(KeyVerification.hash1.bytes, hash1, 32);
    meshtastic_MeshPacket *p = allocDataProtobuf(KeyVerification);
    p->to = currentRemoteNode;
    p->channel = 0;
    p->pki_encrypted = true;
    p->decoded.want_response = true;
    p->priority = meshtastic_MeshPacket_Priority_HIGH;
    service->sendToMesh(p, RX_SRC_LOCAL, true);
    currentState = KEY_VERIFICATION_SENDER_AWAITING_USER;
    // send the toPhone packet
    return;

    // do the hash calculation, and compare to the saved hash2
    //
}

void KeyVerificationModule::updateState()
{
    if (currentState != KEY_VERIFICATION_IDLE) {
        // check for the 30 second timeout
        if (currentNonceTimestamp < getTime() - 30) {
            resetToIdle();
        }
    }
}

void KeyVerificationModule::resetToIdle()
{
    memset(hash1, 0, 32);
    memset(hash2, 0, 32);
    currentNonce = 0;
    currentNonceTimestamp = 0;
    currentSecurityNumber = 0;
    currentRemoteNode = 0;
    currentState = KEY_VERIFICATION_IDLE;
    if (screen)
        screen->endAlert();
}
