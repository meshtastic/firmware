#if !(MESHTASTIC_EXCLUDE_PQ_CRYPTO)

#include "PQKeyExchangeModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "main.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "meshUtils.h"
#include <SHA256.h>

PQKeyExchangeModule *pqKeyExchangeModule;

PQKeyExchangeModule::PQKeyExchangeModule()
    : ProtobufModule("PQKeyExchange", meshtastic_PortNum_PQ_KEY_EXCHANGE_APP, &meshtastic_PQKeyExchange_msg)
{
    ourPortNum = meshtastic_PortNum_PQ_KEY_EXCHANGE_APP;
    nextSessionId = random(1000, 999999); // Start with random session ID
}

PQKeyExchangeModule::~PQKeyExchangeModule()
{
    // Clean up any active sessions
    activeSessions.clear();
}

/**
 * This is the key method - handles all incoming PQ key exchange packets
 * The packet routing system calls this when it receives a packet with 
 * portnum == meshtastic_PortNum_PQ_KEY_EXCHANGE_APP
 */
bool PQKeyExchangeModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_PQKeyExchange *pqex)
{
    // Clean up expired sessions first
    cleanupExpiredSessions();

    // Validate the packet
    if (!pqex) {
        LOG_ERROR("PQ Key Exchange: Received null protobuf");
        return false;
    }

    LOG_INFO("PQ Key Exchange: Received packet from 0x%x, state=%d, session_id=%u", 
             mp.from, pqex->state, pqex->session_id);

    // Handle based on the exchange state
    switch (pqex->state) {
        case meshtastic_PQKeyExchangeState_PQ_KEY_CAPABILITY_ANNOUNCE:
            return handleCapabilityAnnouncement(mp, pqex);

        case meshtastic_PQKeyExchangeState_PQ_KEY_EXCHANGE_REQUEST:
            return handleKeyExchangeRequest(mp, pqex);

        case meshtastic_PQKeyExchangeState_PQ_KEY_FRAGMENT_TRANSFER:
            return handleKeyFragment(mp, pqex);

        case meshtastic_PQKeyExchangeState_PQ_KEY_CONFIRM:
            return handleKeyConfirm(mp, pqex);

        default:
            LOG_WARN("PQ Key Exchange: Unknown state %d from 0x%x", pqex->state, mp.from);
            return false;
    }
}

bool PQKeyExchangeModule::handleCapabilityAnnouncement(const meshtastic_MeshPacket &mp, meshtastic_PQKeyExchange *pqex)
{
    LOG_INFO("PQ Key Exchange: Capability announcement from 0x%x, capabilities=0x%x", 
             mp.from, pqex->capabilities);

    // Store the remote node's PQ capabilities
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(mp.from);
    if (!node) {
        LOG_ERROR("PQ Key Exchange: Unknown node 0x%x", mp.from);
        return false;
    }

    // Check if remote node supports Kyber
    if (!(pqex->capabilities & PQ_CAP_KYBER_SUPPORT)) {
        LOG_INFO("PQ Key Exchange: Node 0x%x does not support Kyber", mp.from);
        return true; // Not an error, just not supported
    }

    // If we don't have valid keys for this node and both nodes support PQ,
    // we could optionally initiate a key exchange here
    if (!hasValidPQKeys(mp.from) && (getPQCapabilities() & PQ_CAP_KYBER_SUPPORT)) {
        LOG_INFO("PQ Key Exchange: Could initiate exchange with 0x%x", mp.from);
        // For now, just log it. Actual initiation would typically be triggered
        // by the application layer or when we need to send an encrypted packet
    }

    return true;
}

bool PQKeyExchangeModule::handleKeyExchangeRequest(const meshtastic_MeshPacket &mp, meshtastic_PQKeyExchange *pqex)
{
    LOG_INFO("PQ Key Exchange: Key exchange request from 0x%x, session_id=%u", 
             mp.from, pqex->session_id);

    // Check if we support PQ crypto
    if (!(getPQCapabilities() & PQ_CAP_KYBER_SUPPORT)) {
        LOG_WARN("PQ Key Exchange: We don't support Kyber, ignoring request from 0x%x", mp.from);
        return false;
    }

    // Create a new session for this exchange (we're the responder)
    PQKeyExchangeSession* session = createSession(mp.from, false);
    if (!session) {
        LOG_ERROR("PQ Key Exchange: Failed to create session for 0x%x", mp.from);
        return false;
    }

    session->sessionId = pqex->session_id;
    session->state = meshtastic_PQKeyExchangeState_PQ_KEY_EXCHANGE_REQUEST;

    // Generate our Kyber key pair
    uint8_t ourPublicKey[PQCrypto::Kyber::PublicKeySize];
    uint8_t ourPrivateKey[PQCrypto::Kyber::PrivateKeySize];
    
    if (!crypto->generateKyberKeyPair(ourPublicKey, ourPrivateKey)) {
        LOG_ERROR("PQ Key Exchange: Failed to generate Kyber key pair");
        activeSessions.erase(session->sessionId);
        return false;
    }

    // Calculate how many fragments we need for our public key
    const size_t keySize = PQCrypto::Kyber::PublicKeySize;
    const uint32_t totalFragments = (keySize + PQ_KEY_FRAGMENT_SIZE - 1) / PQ_KEY_FRAGMENT_SIZE;

    LOG_INFO("PQ Key Exchange: Sending %u fragments of %zu byte key to 0x%x", 
             totalFragments, keySize, mp.from);

    // Send our public key in fragments
    for (uint32_t i = 0; i < totalFragments; i++) {
        const size_t fragmentStart = i * PQ_KEY_FRAGMENT_SIZE;
        const size_t fragmentSize = min(PQ_KEY_FRAGMENT_SIZE, keySize - fragmentStart);
        
        if (!sendKeyFragment(mp.from, session->sessionId, 
                           ourPublicKey + fragmentStart, keySize, i, totalFragments)) {
            LOG_ERROR("PQ Key Exchange: Failed to send fragment %u to 0x%x", i, mp.from);
            activeSessions.erase(session->sessionId);
            return false;
        }
    }

    session->state = meshtastic_PQKeyExchangeState_PQ_KEY_FRAGMENT_TRANSFER;
    return true;
}

bool PQKeyExchangeModule::handleKeyFragment(const meshtastic_MeshPacket &mp, meshtastic_PQKeyExchange *pqex)
{
    LOG_DEBUG("PQ Key Exchange: Fragment %u/%u from 0x%x, session_id=%u", 
              pqex->sequence + 1, pqex->total_fragments, mp.from, pqex->session_id);

    // Find the session
    PQKeyExchangeSession* session = findSession(pqex->session_id);
    if (!session) {
        LOG_ERROR("PQ Key Exchange: No session found for ID %u", pqex->session_id);
        return false;
    }

    // Verify the fragment is from the expected node
    if (session->remoteNode != mp.from) {
        LOG_ERROR("PQ Key Exchange: Fragment from wrong node 0x%x (expected 0x%x)", 
                  mp.from, session->remoteNode);
        return false;
    }

    // Initialize fragment tracking on first fragment
    if (pqex->sequence == 0) {
        session->expectedFragments = pqex->total_fragments;
        session->totalKeySize = PQCrypto::Kyber::PublicKeySize; // We know Kyber key size
        session->receivedFragments = 0;
        memset(session->keyBuffer, 0, sizeof(session->keyBuffer));
        
        // Store expected key hash if provided
        if (pqex->key_hash.size == 32) {
            memcpy(session->expectedKeyHash, pqex->key_hash.bytes, 32);
        }
    }

    // Validate fragment sequence
    if (pqex->sequence >= session->expectedFragments) {
        LOG_ERROR("PQ Key Exchange: Invalid fragment sequence %u (expected < %u)", 
                  pqex->sequence, session->expectedFragments);
        return false;
    }

    // Copy fragment data to key buffer
    const size_t fragmentStart = pqex->sequence * PQ_KEY_FRAGMENT_SIZE;
    const size_t maxCopySize = min((size_t)pqex->data.size, 
                                   sizeof(session->keyBuffer) - fragmentStart);
    
    if (maxCopySize > 0 && fragmentStart < sizeof(session->keyBuffer)) {
        memcpy(session->keyBuffer + fragmentStart, pqex->data.bytes, maxCopySize);
        session->receivedFragments++;
        session->lastActivity = getTime();
        
        LOG_DEBUG("PQ Key Exchange: Received fragment %u, total received: %u/%u", 
                  pqex->sequence, session->receivedFragments, session->expectedFragments);
    }

    // Check if we have all fragments
    if (session->receivedFragments >= session->expectedFragments) {
        LOG_INFO("PQ Key Exchange: All fragments received from 0x%x", mp.from);
        
        // Verify the assembled key
        if (!verifyKeyFragments(session)) {
            LOG_ERROR("PQ Key Exchange: Key verification failed");
            activeSessions.erase(session->sessionId);
            return false;
        }

        // Complete the key exchange
        if (!completeKeyExchange(session)) {
            LOG_ERROR("PQ Key Exchange: Failed to complete key exchange");
            activeSessions.erase(session->sessionId);
            return false;
        }

        // Send confirmation
        meshtastic_PQKeyExchange confirmation = meshtastic_PQKeyExchange_init_zero;
        confirmation.state = meshtastic_PQKeyExchangeState_PQ_KEY_CONFIRM;
        confirmation.session_id = session->sessionId;
        
        meshtastic_MeshPacket *confirmPacket = allocDataProtobuf(confirmation);
        confirmPacket->to = mp.from;
        confirmPacket->decoded.want_response = false;
        service->sendToMesh(confirmPacket, RX_SRC_LOCAL);

        LOG_INFO("PQ Key Exchange: Completed exchange with 0x%x", mp.from);
        activeSessions.erase(session->sessionId);
    }

    return true;
}

bool PQKeyExchangeModule::handleKeyConfirm(const meshtastic_MeshPacket &mp, meshtastic_PQKeyExchange *pqex)
{
    LOG_INFO("PQ Key Exchange: Confirmation from 0x%x, session_id=%u", mp.from, pqex->session_id);

    PQKeyExchangeSession* session = findSession(pqex->session_id);
    if (session) {
        LOG_INFO("PQ Key Exchange: Session %u confirmed and completed", pqex->session_id);
        activeSessions.erase(session->sessionId);
    }

    return true;
}

bool PQKeyExchangeModule::verifyKeyFragments(PQKeyExchangeSession* session)
{
    // Basic size check
    if (session->totalKeySize != PQCrypto::Kyber::PublicKeySize) {
        LOG_ERROR("PQ Key Exchange: Invalid key size %u (expected %u)", 
                  session->totalKeySize, (uint32_t)PQCrypto::Kyber::PublicKeySize);
        return false;
    }

    // Hash verification if provided
    if (memcmp(session->expectedKeyHash, "\\0\\0\\0\\0\\0\\0\\0\\0", 8) != 0) {
        SHA256 hash;
        uint8_t computedHash[32];
        hash.update(session->keyBuffer, session->totalKeySize);
        hash.finalize(computedHash, 32);
        
        if (memcmp(computedHash, session->expectedKeyHash, 32) != 0) {
            LOG_ERROR("PQ Key Exchange: Key hash verification failed");
            return false;
        }
    }

    LOG_INFO("PQ Key Exchange: Key fragments verified successfully");
    return true;
}

bool PQKeyExchangeModule::completeKeyExchange(PQKeyExchangeSession* session)
{
    // Store the remote public key in NodeDB
    if (!storePQKeys(session->remoteNode, session->keyBuffer, session->totalKeySize)) {
        LOG_ERROR("PQ Key Exchange: Failed to store PQ keys for 0x%x", session->remoteNode);
        return false;
    }

    LOG_INFO("PQ Key Exchange: Keys stored for node 0x%x", session->remoteNode);
    return true;
}

// Helper methods
uint32_t PQKeyExchangeModule::generateSessionId()
{
    return nextSessionId++;
}

PQKeyExchangeSession* PQKeyExchangeModule::createSession(NodeNum remoteNode, bool isInitiator)
{
    uint32_t sessionId = generateSessionId();
    PQKeyExchangeSession session = {};
    
    session.remoteNode = remoteNode;
    session.sessionId = sessionId;
    session.state = meshtastic_PQKeyExchangeState_PQ_KEY_IDLE;
    session.expectedFragments = 0;
    session.receivedFragments = 0;
    session.lastActivity = getTime();
    session.totalKeySize = 0;
    session.isInitiator = isInitiator;
    memset(session.keyBuffer, 0, sizeof(session.keyBuffer));
    memset(session.expectedKeyHash, 0, sizeof(session.expectedKeyHash));

    activeSessions[sessionId] = session;
    return &activeSessions[sessionId];
}

PQKeyExchangeSession* PQKeyExchangeModule::findSession(uint32_t sessionId)
{
    auto it = activeSessions.find(sessionId);
    return (it != activeSessions.end()) ? &it->second : nullptr;
}

void PQKeyExchangeModule::cleanupExpiredSessions()
{
    uint32_t currentTime = getTime();
    auto it = activeSessions.begin();
    
    while (it != activeSessions.end()) {
        if ((currentTime - it->second.lastActivity) > PQ_SESSION_TIMEOUT_SECS) {
            LOG_INFO("PQ Key Exchange: Cleaning up expired session %u", it->first);
            it = activeSessions.erase(it);
        } else {
            ++it;
        }
    }
}

uint32_t PQKeyExchangeModule::getPQCapabilities()
{
    uint32_t caps = 0;
    
    // Check if we have Kyber support compiled in and hardware capable
    if (crypto && crypto->hasValidKyberKeys()) {
        caps |= PQ_CAP_KYBER_SUPPORT;
    }
    
    // Add preference flag if configured
    // TODO: Add config option for PQ preference
    caps |= PQ_CAP_PREFER_PQ;
    
    return caps;
}

bool PQKeyExchangeModule::getPQKeyHash(uint8_t hash[32])
{
    if (!crypto || !crypto->hasValidKyberKeys()) {
        return false;
    }
    
    uint8_t publicKey[PQCrypto::Kyber::PublicKeySize];
    if (!crypto->getKyberPublicKey(publicKey)) {
        return false;
    }
    
    SHA256 sha;
    sha.update(publicKey, sizeof(publicKey));
    sha.finalize(hash, 32);
    
    return true;
}

bool PQKeyExchangeModule::hasValidPQKeys(NodeNum remoteNode)
{
    // TODO: Check NodeDB for stored PQ keys
    // For now, return false - this would be implemented when we
    // extend NodeDB to store PQ keys
    return false;
}

bool PQKeyExchangeModule::storePQKeys(NodeNum remoteNode, const uint8_t* publicKey, size_t keySize)
{
    // TODO: Store PQ keys in NodeDB
    // This requires extending the NodeDB structure to include PQ keys
    LOG_INFO("PQ Key Exchange: Would store %zu byte key for node 0x%x", keySize, remoteNode);
    return true; // Placeholder
}

// Stub implementations for methods we'll implement later
bool PQKeyExchangeModule::sendKeyFragment(NodeNum remoteNode, uint32_t sessionId, const uint8_t* keyData, 
                                        size_t totalSize, uint32_t sequence, uint32_t totalFragments)
{
    // Implementation for sending fragments - we'll add this next
    return true;
}

bool PQKeyExchangeModule::initiateKeyExchange(NodeNum remoteNode)
{
    // Implementation for initiating exchange - we'll add this next
    return true;
}

AdminMessageHandleResult PQKeyExchangeModule::handleAdminMessageForModule(const meshtastic_MeshPacket &mp,
                                                                        meshtastic_AdminMessage *request,
                                                                        meshtastic_AdminMessage *response)
{
    return AdminMessageHandleResult::NOT_HANDLED;
}

meshtastic_MeshPacket *PQKeyExchangeModule::allocReply()
{
    // Default implementation - specific replies handled in message handlers
    return nullptr;
}

#endif // !(MESHTASTIC_EXCLUDE_PQ_CRYPTO)
