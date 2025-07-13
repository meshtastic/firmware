#if !MESHTASTIC_EXCLUDE_PKI
#include "KeyVerificationModule.h"
#include "MeshService.h"
#include "RTC.h"
#include "graphics/draw/MenuHandler.h"
#include "main.h"
#include "meshUtils.h"
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
    if (request->which_payload_variant == meshtastic_AdminMessage_key_verification_tag && mp.from == 0) {
        LOG_WARN("Handling Key Verification Admin Message type %u", request->key_verification.message_type);

        if (request->key_verification.message_type == meshtastic_KeyVerificationAdmin_MessageType_INITIATE_VERIFICATION &&
            currentState == KEY_VERIFICATION_IDLE) {
            sendInitialRequest(request->key_verification.remote_nodenum);

        } else if (request->key_verification.message_type ==
                       meshtastic_KeyVerificationAdmin_MessageType_PROVIDE_SECURITY_NUMBER &&
                   request->key_verification.has_security_number && currentState == KEY_VERIFICATION_SENDER_AWAITING_NUMBER &&
                   request->key_verification.nonce == currentNonce) {
            processSecurityNumber(request->key_verification.security_number);

        } else if (request->key_verification.message_type == meshtastic_KeyVerificationAdmin_MessageType_DO_VERIFY &&
                   request->key_verification.nonce == currentNonce) {
            auto remoteNodePtr = nodeDB->getMeshNode(currentRemoteNode);
            remoteNodePtr->bitfield |= NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_MASK;
            resetToIdle();
        } else if (request->key_verification.message_type == meshtastic_KeyVerificationAdmin_MessageType_DO_NOT_VERIFY) {
            resetToIdle();
        }
        return AdminMessageHandleResult::HANDLED;
    }
    return AdminMessageHandleResult::NOT_HANDLED;
}

bool KeyVerificationModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_KeyVerification *r)
{
    updateState();
    if (mp.pki_encrypted == false) {
        return false;
    }
    if (mp.from != currentRemoteNode) { // because the inital connection request is handled in allocReply()
        return false;
    }
    if (currentState == KEY_VERIFICATION_IDLE) {
        return false; // if we're idle, the only acceptable message is an init, which should be handled by allocReply()
    }

    if (currentState == KEY_VERIFICATION_SENDER_HAS_INITIATED && r->nonce == currentNonce && r->hash2.size == 32 &&
        r->hash1.size == 0) {
        memcpy(hash2, r->hash2.bytes, 32);
        IF_SCREEN(screen->showNumberPicker("Enter Security Number", 60000, 6, [](int number_picked) -> void {
            keyVerificationModule->processSecurityNumber(number_picked);
        });)

        meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
        cn->level = meshtastic_LogRecord_Level_WARNING;
        sprintf(cn->message, "Enter Security Number for Key Verification");
        cn->which_payload_variant = meshtastic_ClientNotification_key_verification_number_request_tag;
        cn->payload_variant.key_verification_number_request.nonce = currentNonce;
        strncpy(cn->payload_variant.key_verification_number_request.remote_longname, // should really check for nulls, etc
                nodeDB->getMeshNode(currentRemoteNode)->user.long_name,
                sizeof(cn->payload_variant.key_verification_number_request.remote_longname));
        service->sendClientNotification(cn);
        LOG_INFO("Received hash2");
        currentState = KEY_VERIFICATION_SENDER_AWAITING_NUMBER;
        return true;

    } else if (currentState == KEY_VERIFICATION_RECEIVER_AWAITING_HASH1 && r->hash1.size == 32 && r->nonce == currentNonce) {
        if (memcmp(hash1, r->hash1.bytes, 32) == 0) {
            memset(message, 0, sizeof(message));
            sprintf(message, "Verification: \n");
            generateVerificationCode(message + 15);
            LOG_INFO("Hash1 matches!");
            static const char *optionsArray[] = {"Reject", "Accept"};
            // Don't try to put the array definition in the macro. Does not work with curly braces.
            IF_SCREEN(graphics::BannerOverlayOptions options; options.message = message; options.durationMs = 30000;
                      options.optionsArrayPtr = optionsArray; options.optionsCount = 2;
                      options.notificationType = graphics::notificationTypeEnum::selection_picker;
                      options.bannerCallback =
                          [=](int selected) {
                              if (selected == 1) {
                                  auto remoteNodePtr = nodeDB->getMeshNode(currentRemoteNode);
                                  remoteNodePtr->bitfield |= NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_MASK;
                              }
                          };
                      screen->showOverlayBanner(options);)
            meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
            cn->level = meshtastic_LogRecord_Level_WARNING;
            sprintf(cn->message, "Final confirmation for incoming manual key verification %s", message);
            cn->which_payload_variant = meshtastic_ClientNotification_key_verification_final_tag;
            cn->payload_variant.key_verification_final.nonce = currentNonce;
            strncpy(cn->payload_variant.key_verification_final.remote_longname, // should really check for nulls, etc
                    nodeDB->getMeshNode(currentRemoteNode)->user.long_name,
                    sizeof(cn->payload_variant.key_verification_final.remote_longname));
            cn->payload_variant.key_verification_final.isSender = false;
            service->sendClientNotification(cn);

            currentState = KEY_VERIFICATION_RECEIVER_AWAITING_USER;
            return true;
        }
    }
    return false;
}

bool KeyVerificationModule::sendInitialRequest(NodeNum remoteNode)
{
    LOG_DEBUG("keyVerification start");
    // generate nonce
    updateState();
    if (currentState != KEY_VERIFICATION_IDLE) {
        IF_SCREEN(graphics::menuHandler::menuQueue = graphics::menuHandler::throttle_message;)
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
    if (currentState != KEY_VERIFICATION_IDLE) { // TODO: cooldown period
        LOG_WARN("Key Verification requested, but already in a request");
        return nullptr;
    } else if (!currentRequest->pki_encrypted) {
        LOG_WARN("Key Verification requested, but not in a PKI packet");
        return nullptr;
    }
    currentState = KEY_VERIFICATION_RECEIVER_AWAITING_HASH1;

    auto req = *currentRequest;
    const auto &p = req.decoded;
    meshtastic_KeyVerification scratch;
    meshtastic_KeyVerification response;
    meshtastic_MeshPacket *responsePacket = nullptr;
    pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_KeyVerification_msg, &scratch);

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
    IF_SCREEN(snprintf(message, 25, "Security Number \n%03u %03u", currentSecurityNumber / 1000, currentSecurityNumber % 1000);
              screen->showSimpleBanner(message, 30000); LOG_WARN("%s", message);)
    meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
    cn->level = meshtastic_LogRecord_Level_WARNING;
    sprintf(cn->message, "Incoming Key Verification.\nSecurity Number\n%03u %03u", currentSecurityNumber / 1000,
            currentSecurityNumber % 1000);
    cn->which_payload_variant = meshtastic_ClientNotification_key_verification_number_inform_tag;
    cn->payload_variant.key_verification_number_inform.nonce = currentNonce;
    strncpy(cn->payload_variant.key_verification_number_inform.remote_longname, // should really check for nulls, etc
            nodeDB->getMeshNode(currentRemoteNode)->user.long_name,
            sizeof(cn->payload_variant.key_verification_number_inform.remote_longname));
    cn->payload_variant.key_verification_number_inform.security_number = currentSecurityNumber;
    service->sendClientNotification(cn);
    LOG_WARN("Security Number %04u, nonce %llu", currentSecurityNumber, currentNonce);
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
    IF_SCREEN(screen->requestMenu(graphics::menuHandler::key_verification_final_prompt);)
    meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
    cn->level = meshtastic_LogRecord_Level_WARNING;
    sprintf(cn->message, "Final confirmation for outgoing manual key verification %s", message);
    cn->which_payload_variant = meshtastic_ClientNotification_key_verification_final_tag;
    cn->payload_variant.key_verification_final.nonce = currentNonce;
    strncpy(cn->payload_variant.key_verification_final.remote_longname, // should really check for nulls, etc
            nodeDB->getMeshNode(currentRemoteNode)->user.long_name,
            sizeof(cn->payload_variant.key_verification_final.remote_longname));
    cn->payload_variant.key_verification_final.isSender = true;
    service->sendClientNotification(cn);
    LOG_INFO(message);

    return;
}

void KeyVerificationModule::updateState()
{
    if (currentState != KEY_VERIFICATION_IDLE) {
        // check for the 60 second timeout
        if (currentNonceTimestamp < getTime() - 60) {
            resetToIdle();
        } else {
            currentNonceTimestamp = getTime();
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
}

void KeyVerificationModule::generateVerificationCode(char *readableCode)
{
    for (int i = 0; i < 4; i++) {
        // drop the two highest significance bits, then encode as a base64
        readableCode[i] = (hash1[i] >> 2) + 48; // not a standardized base64, but workable and avoids having a dictionary.
    }
    readableCode[4] = ' ';
    for (int i = 5; i < 9; i++) {
        // drop the two highest significance bits, then encode as a base64
        readableCode[i] = (hash1[i] >> 2) + 48; // not a standardized base64, but workable and avoids having a dictionary.
    }
}
#endif