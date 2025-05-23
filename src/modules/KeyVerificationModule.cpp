#include "KeyVerificationModule.h"
#include "RTC.h"
#include "modules/AdminModule.h"

KeyVerificationModule::KeyVerificationModule()
    : SinglePortModule("KeyVerificationModule", meshtastic_PortNum_KEY_VERIFICATION_APP)
{
}

AdminMessageHandleResult KeyVerificationModule::handleAdminMessageForModule(const meshtastic_MeshPacket &mp,
                                                                            meshtastic_AdminMessage *request,
                                                                            meshtastic_AdminMessage *response)
{
    if (request->which_payload_variant == meshtastic_AdminMessage_key_verification_tag) {
        LOG_DEBUG("Handling Key Verification Admin Message");
        if (mp.from == 0) {
            meshtastic_MeshPacket *p = allocDataPacket();
            // check current state, do rate limiting.
        }
        return AdminMessageHandleResult::HANDLED;
    }
    return AdminMessageHandleResult::NOT_HANDLED;
}

// handle messages to this port

bool KeyVerificationModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_KeyVerification *r)
{
    // for each incoming message, do the state timeout check
    // then if the state is not idle, sanity check for the same nonce and the right current state for the received message
    //
    meshtastic_MeshPacket *p = allocDataPacket();
}

bool KeyVerificationModule::sendInitialRequest(NodeNum remoteNode)
{
    // generate nonce
    currentNonce = random(1, __UINT64_MAX__);
    currentNonceTimestamp = getTime();
    currentRemoteNode = remoteNode;
}

bool KeyVerificationModule::sendResponse(const meshtastic_MeshPacket &mp, meshtastic_KeyVerification *r)
{
    currentNonce = r->nonce;
}