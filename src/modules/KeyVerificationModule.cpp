#if !MESHTASTIC_EXCLUDE_PKI
#include "KeyVerificationModule.h"
#include "CryptoEngine.h"
#include "HardwareRNG.h"
#include "MeshService.h"
#include "gps/RTC.h"
#include "graphics/draw/MenuHandler.h"
#include "main.h"
#include "mesh/Throttle.h"
#include "meshUtils.h"
#include "modules/AdminModule.h"
#include "modules/NodeInfoModule.h"
#include <RNG.h>
#include <SHA256.h>

#define KEY_VERIFICATION_TIMEOUT_SECS 60
// Hard cap on one session, independent of the refreshable idle timeout above.
#define KEY_VERIFICATION_MAX_SESSION_MS (3 * 60 * 1000UL)
// Minimum spacing between remote-initiated sessions.
#define KEY_VERIFICATION_REMOTE_COOLDOWN_MS (60 * 1000UL)

KeyVerificationModule *keyVerificationModule;

namespace
{
void copyNodeLongNameOrUnknown(char *dest, size_t destSize, const meshtastic_NodeInfoLite *node)
{
    if (!dest || destSize == 0)
        return;
    const char *name = (node && nodeInfoLiteHasUser(node) && node->long_name[0]) ? node->long_name : "Unknown";
    strncpy(dest, name, destSize - 1);
    dest[destSize - 1] = '\0';
}
} // namespace

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
        LOG_DEBUG("Handling Key Verification Admin Message type %u", request->key_verification.message_type);

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
            commitVerifiedRemoteNode();
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
    // No refresh here: this runs before the sender is checked, so any node could hold the session open.
    updateState(false);
    // Note: pki_encrypted is not required here. The first response (M2) may arrive channel-encrypted in
    // the bootstrap case; the follow-on hash1 packet (M3) is required to be PKI in its branch below.
    if (mp.from != currentRemoteNode) { // because the inital connection request is handled in allocReply()
        return false;
    }
    if (currentState == KEY_VERIFICATION_IDLE) {
        return false; // if we're idle, the only acceptable message is an init, which should be handled by allocReply()
    }

    if (currentState == KEY_VERIFICATION_SENDER_HAS_INITIATED && r->nonce == currentNonce && r->hash2.size == 32 &&
        r->hash1.size == 32) {
        memcpy(hash2, r->hash2.bytes, 32);
        // The response carries the responder's public key in hash1. If we don't already hold it, stash it
        // as a pending key so the Router can PKI-encrypt our follow-on packet (committed to NodeDB on accept).
        auto *responderNode = nodeDB->getMeshNode(currentRemoteNode);
        if (responderNode == nullptr || responderNode->public_key.size != 32)
            crypto->setPendingPublicKey(currentRemoteNode, r->hash1.bytes);
        IF_SCREEN(screen->showNumberPicker("Enter Security Number", 60000, 6, false, [](uint32_t number_picked) -> void {
            keyVerificationModule->processSecurityNumber(number_picked);
        });)

        meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
        if (cn) {
            cn->level = meshtastic_LogRecord_Level_WARNING;
            snprintf(cn->message, sizeof(cn->message), "Enter Security Number for Key Verification");
            cn->which_payload_variant = meshtastic_ClientNotification_key_verification_number_request_tag;
            cn->payload_variant.key_verification_number_request.nonce = currentNonce;
            copyNodeLongNameOrUnknown(cn->payload_variant.key_verification_number_request.remote_longname,
                                      sizeof(cn->payload_variant.key_verification_number_request.remote_longname),
                                      nodeDB->getMeshNode(currentRemoteNode));
            service->sendClientNotification(cn);
        }
        LOG_INFO("Received hash2");
        currentNonceTimestamp = getTime(); // the protocol advanced, so extend the deadline
        currentState = KEY_VERIFICATION_SENDER_AWAITING_NUMBER;
        return true;

    } else if (currentState == KEY_VERIFICATION_RECEIVER_AWAITING_HASH1 && mp.pki_encrypted && r->hash1.size == 32 &&
               r->nonce == currentNonce) {
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
                              LOG_DEBUG("User selected %d for key verification", selected);
                              if (selected == 1) {
                                  keyVerificationModule->commitVerifiedRemoteNode();
                              }
                          };
                      screen->showOverlayBanner(options);)
            meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
            if (cn) {
                cn->level = meshtastic_LogRecord_Level_WARNING;
                snprintf(cn->message, sizeof(cn->message), "Final confirmation for incoming manual key verification %s", message);
                cn->which_payload_variant = meshtastic_ClientNotification_key_verification_final_tag;
                cn->payload_variant.key_verification_final.nonce = currentNonce;
                copyNodeLongNameOrUnknown(cn->payload_variant.key_verification_final.remote_longname,
                                          sizeof(cn->payload_variant.key_verification_final.remote_longname),
                                          nodeDB->getMeshNode(currentRemoteNode));
                cn->payload_variant.key_verification_final.isSender = false;
                service->sendClientNotification(cn);
            }

            currentNonceTimestamp = getTime(); // the protocol advanced, so extend the deadline
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
    updateState(false);
    if (currentState != KEY_VERIFICATION_IDLE) {
        IF_SCREEN(graphics::menuHandler::menuQueue = graphics::menuHandler::ThrottleMessage;)
        return false;
    }
    updateState(true);
    // The nonce binds the handshake, so draw it from the hardware RNG (falling back to the CSPRNG)
    // under cryptLock, as allocReply does for the security number. random() is both predictable and
    // only 32 bits wide, leaving half of this nonce zero.
    {
        concurrency::LockGuard g(cryptLock);
        if (!HardwareRNG::fill((uint8_t *)&currentNonce, sizeof(currentNonce)))
            CryptRNG.rand((uint8_t *)&currentNonce, sizeof(currentNonce));
    }
    currentNonceTimestamp = getTime();
    sessionStartedMs = millis();
    currentRemoteNode = remoteNode;
    meshtastic_KeyVerification KeyVerification = meshtastic_KeyVerification_init_zero;
    KeyVerification.nonce = currentNonce;
    KeyVerification.hash2.size = 0;
    // Carry our public key in the otherwise-unused hash1 field so a peer that does not yet hold our
    // key can learn it from this first message (bootstrap / onboarding).
    KeyVerification.hash1.size = 32;
    memcpy(KeyVerification.hash1.bytes, owner.public_key.bytes, 32);
    meshtastic_MeshPacket *p = allocDataProtobuf(KeyVerification);
    if (!p)
        return false;
    p->to = remoteNode;
    p->channel = 0;
    // Only request PKI when we already hold the destination's key. Otherwise this first message goes out
    // channel-encrypted (the Router falls back) so the peer can bootstrap from the key carried in hash1.
    auto *remoteNodePtr = nodeDB->getMeshNode(remoteNode);
    p->pki_encrypted = (remoteNodePtr != nullptr && remoteNodePtr->public_key.size == 32);
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
        ignoreRequest = true; // do not let the busy path emit a NAK back to the requester
        return nullptr;
    }
    // Opening a session raises a banner and locks the only slot, before the peer has authenticated.
    if (lastRemoteSessionMs != 0 && Throttle::isWithinTimespanMs(lastRemoteSessionMs, KEY_VERIFICATION_REMOTE_COOLDOWN_MS)) {
        LOG_WARN("Key Verification requested, but within cooldown");
        ignoreRequest = true;
        return nullptr;
    }
    sessionStartedMs = millis();
    sessionFromRemote = true;
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
    // The security number is the handshake's MitM-resistance entropy, so draw it from the hardware
    // RNG (falling back to the CSPRNG used for key material), never the predictable random().
    // Hold cryptLock like xeddsa_sign does: on nRF52 the fill toggles the CC310 that packet crypto
    // on the BLE task also uses, and the CryptRNG state is shared with the signing path.
    uint32_t securityEntropy = 0;
    {
        concurrency::LockGuard g(cryptLock);
        if (!HardwareRNG::fill((uint8_t *)&securityEntropy, sizeof(securityEntropy)))
            CryptRNG.rand((uint8_t *)&securityEntropy, sizeof(securityEntropy));
    }
    currentSecurityNumber = (securityEntropy % 999999) + 1;

    // Resolve the requester's public key: from the PKI envelope, else carried in hash1 (bootstrap).
    // Stash unknown keys as pending (committed to NodeDB only once verification is accepted).
    const uint8_t *senderKey = nullptr;
    if (currentRequest->pki_encrypted && currentRequest->public_key.size == 32) {
        senderKey = currentRequest->public_key.bytes; // this is bizarre, fixme
    } else if (scratch.hash1.size == 32) {
        senderKey = scratch.hash1.bytes;
    }
    if (senderKey == nullptr) {
        LOG_WARN("Key Verification request without a usable public key");
        resetToIdle();
        return nullptr;
    }
    auto *senderNode = nodeDB->getMeshNode(currentRemoteNode);
    bool senderKeyInNodeDB = (senderNode != nullptr && senderNode->public_key.size == 32);
    if (!senderKeyInNodeDB)
        crypto->setPendingPublicKey(currentRemoteNode, senderKey);

    // generate local hash1
    hash.reset();
    hash.update(&currentSecurityNumber, sizeof(currentSecurityNumber));
    hash.update(&currentNonce, sizeof(currentNonce));
    hash.update(&currentRemoteNode, sizeof(currentRemoteNode));
    hash.update(&ourNodeNum, sizeof(ourNodeNum));
    hash.update(senderKey, 32);
    hash.update(owner.public_key.bytes, owner.public_key.size);
    hash.finalize(hash1, 32);

    // generate hash2
    hash.reset();
    hash.update(&currentNonce, sizeof(currentNonce));
    hash.update(hash1, 32);
    hash.finalize(hash2, 32);
    // Carry our public key in hash1 of the response so the requester can bootstrap our key as well.
    response.hash1.size = 32;
    memcpy(response.hash1.bytes, owner.public_key.bytes, 32);
    response.hash2.size = 32;
    memcpy(response.hash2.bytes, hash2, 32);

    responsePacket = allocDataProtobuf(response);
    if (!responsePacket) {
        LOG_WARN("Key Verification response allocation failed");
        ignoreRequest = true;
        resetToIdle();
        return nullptr;
    }

    // PKI-encrypt the response only if we already held the requester's key. In the bootstrap case it goes
    // out channel-encrypted so the requester (who lacks our key) can decode it and read hash1.
    responsePacket->pki_encrypted = senderKeyInNodeDB;
    IF_SCREEN(snprintf(message, 25, "Security Number \n%03u %03u", currentSecurityNumber / 1000, currentSecurityNumber % 1000);
              screen->showSimpleBanner(message, 30000); LOG_DEBUG("%s", message);)
    meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
    if (cn) {
        cn->level = meshtastic_LogRecord_Level_WARNING;
        snprintf(cn->message, sizeof(cn->message), "Incoming Key Verification.\nSecurity Number\n%03u %03u",
                 currentSecurityNumber / 1000, currentSecurityNumber % 1000);
        cn->which_payload_variant = meshtastic_ClientNotification_key_verification_number_inform_tag;
        cn->payload_variant.key_verification_number_inform.nonce = currentNonce;
        copyNodeLongNameOrUnknown(cn->payload_variant.key_verification_number_inform.remote_longname,
                                  sizeof(cn->payload_variant.key_verification_number_inform.remote_longname),
                                  nodeDB->getMeshNode(currentRemoteNode));
        cn->payload_variant.key_verification_number_inform.security_number = currentSecurityNumber;
        service->sendClientNotification(cn);
    }
    LOG_DEBUG("Security Number %04u, nonce %llu", currentSecurityNumber, currentNonce);
    return responsePacket;
}

void KeyVerificationModule::processSecurityNumber(uint32_t incomingNumber)
{
    SHA256 hash;
    NodeNum ourNodeNum = nodeDB->getNodeNum();
    uint8_t scratch_hash[32] = {0};
    LOG_DEBUG("received security number: %u", incomingNumber);
    meshtastic_NodeInfoLite *remoteNodePtr = nodeDB->getMeshNode(currentRemoteNode);
    // Resolve the remote public key: NodeDB if known, otherwise the pending key learned during this
    // handshake (bootstrap case).
    meshtastic_NodeInfoLite_public_key_t remotePublic = {0, {0}};
    if (remoteNodePtr != nullptr && remoteNodePtr->public_key.size == 32) {
        remotePublic = remoteNodePtr->public_key;
    } else if (!crypto->getPendingPublicKey(currentRemoteNode, remotePublic)) {
        LOG_WARN("No public key available for remote node, aborting key verification");
        resetToIdle();
        return;
    }
    // calculate hash1
    hash.reset();
    hash.update(&incomingNumber, sizeof(incomingNumber));
    hash.update(&currentNonce, sizeof(currentNonce));
    hash.update(&ourNodeNum, sizeof(ourNodeNum));
    hash.update(&currentRemoteNode, sizeof(currentRemoteNode));
    hash.update(owner.public_key.bytes, owner.public_key.size);

    hash.update(remotePublic.bytes, 32);
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
    if (!p)
        return;
    p->to = currentRemoteNode;
    p->channel = 0;
    p->pki_encrypted = true;
    p->decoded.want_response = true;
    p->priority = meshtastic_MeshPacket_Priority_HIGH;
    service->sendToMesh(p, RX_SRC_LOCAL, true);
    currentState = KEY_VERIFICATION_SENDER_AWAITING_USER;
    IF_SCREEN(screen->requestMenu(graphics::menuHandler::KeyVerificationFinalPrompt);)
    meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
    if (cn) {
        cn->level = meshtastic_LogRecord_Level_WARNING;
        snprintf(cn->message, sizeof(cn->message), "Final confirmation for outgoing manual key verification %s", message);
        cn->which_payload_variant = meshtastic_ClientNotification_key_verification_final_tag;
        cn->payload_variant.key_verification_final.nonce = currentNonce;
        copyNodeLongNameOrUnknown(cn->payload_variant.key_verification_final.remote_longname,
                                  sizeof(cn->payload_variant.key_verification_final.remote_longname),
                                  nodeDB->getMeshNode(currentRemoteNode));
        cn->payload_variant.key_verification_final.isSender = true;
        service->sendClientNotification(cn);
    }
    LOG_INFO(message);

    return;
}

void KeyVerificationModule::updateState(bool resetTimer)
{
    if (currentState != KEY_VERIFICATION_IDLE) {
        uint32_t now = getTime();
        // Absolute cap first: it is millis() based, so it bounds the session even when the RTC is unset.
        if (!Throttle::isWithinTimespanMs(sessionStartedMs, KEY_VERIFICATION_MAX_SESSION_MS)) {
            resetToIdle();
        } else if (now - currentNonceTimestamp >= KEY_VERIFICATION_TIMEOUT_SECS) {
            resetToIdle();
        } else if (resetTimer) {
            currentNonceTimestamp = now;
        }
    }
}

void KeyVerificationModule::resetToIdle()
{
    memset(hash1, 0, 32);
    memset(hash2, 0, 32);
    if (sessionFromRemote)
        lastRemoteSessionMs = millis(); // start the cooldown when the session ends, not when it opened
    sessionFromRemote = false;
    currentNonce = 0;
    currentNonceTimestamp = 0;
    sessionStartedMs = 0;
    currentSecurityNumber = 0;
    currentRemoteNode = 0;
    currentState = KEY_VERIFICATION_IDLE;
    // Discard any not-yet-verified key learned during this handshake; on reject/timeout it is never trusted.
    crypto->clearPendingPublicKey();
}

void KeyVerificationModule::commitVerifiedRemoteNode()
{
    // The remote node already has a NodeDB entry by this point (packets were exchanged during the
    // handshake), so getMeshNode is sufficient; bail defensively if it is somehow absent.
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(currentRemoteNode);
    if (!node) {
        LOG_WARN("Attempted to commit key, but unknown node");
        return;
    }
    // If we only held the peer's key as a pending (unverified) key during the handshake, commit it to
    // NodeDB now that the user has confirmed the verification, so future PKI traffic can use it.
    meshtastic_NodeInfoLite_public_key_t pending = {0, {0}};
    if (node->public_key.size != 32 && crypto->getPendingPublicKey(currentRemoteNode, pending))
        node->public_key = pending;
    node->bitfield |= NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_MASK;
    // Re-commit via the bare-key primitive: writing the same bytes back is a no-op for the hot
    // store, but it routes the TrafficManagement write-through. ManuallyVerified: the user just
    // confirmed possession of exactly this key - the strongest provenance that cache can carry.
    if (node->public_key.size == 32)
        nodeDB->commitRemoteKey(currentRemoteNode, node->public_key.bytes, NodeDB::KeyCommitTrust::ManuallyVerified);
    LOG_INFO("Node 0x%08x manually verified with security number %u", currentRemoteNode, currentSecurityNumber);
    if (nodeInfoModule)
        nodeInfoModule->sendOurNodeInfo(currentRemoteNode, false, node->channel, true);
    crypto->clearPendingPublicKey();
    currentState = KEY_VERIFICATION_IDLE;
    // Persist the committed key and verified flag so manual verification survives a reboot.
    nodeDB->saveToDisk(SEGMENT_NODEDATABASE);
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
