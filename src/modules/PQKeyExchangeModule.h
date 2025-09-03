#pragma once

#include "ProtobufModule.h"
#include "SinglePortModule.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <map>

#if !(MESHTASTIC_EXCLUDE_PQ_CRYPTO)

// PQ capability flags
#define PQ_CAP_KYBER_SUPPORT     0x01
#define PQ_CAP_PREFER_PQ         0x02

// Maximum fragment size for PQ keys (leaves room for packet headers)
#define PQ_KEY_FRAGMENT_SIZE     200

// Session timeout in seconds
#define PQ_SESSION_TIMEOUT_SECS  300

/**
 * State tracking for ongoing PQ key exchange sessions
 */
struct PQKeyExchangeSession {
    NodeNum remoteNode;
    uint32_t sessionId;
    meshtastic_PQKeyExchangeState state;
    uint32_t expectedFragments;
    uint32_t receivedFragments;
    uint32_t lastActivity; // timestamp
    uint8_t keyBuffer[PQCrypto::Kyber::PublicKeySize]; // Buffer for reassembling fragmented keys
    uint32_t totalKeySize;
    bool isInitiator;
    uint8_t expectedKeyHash[32];
};

/**
 * Module for handling Post-Quantum Key Exchange using Kyber KEM
 * 
 * This module implements an asynchronous key exchange protocol that:
 * 1. Announces PQ capabilities in NodeInfo broadcasts
 * 2. Handles fragmented transmission of large Kyber keys (800 bytes)
 * 3. Manages session state across multiple packets
 * 4. Integrates with existing PKI infrastructure
 */
class PQKeyExchangeModule : public ProtobufModule<meshtastic_PQKeyExchange>
{
  public:
    PQKeyExchangeModule();
    virtual ~PQKeyExchangeModule();

    /**
     * Initiate PQ key exchange with a remote node
     * @param remoteNode Node to exchange keys with
     * @return true if exchange initiated successfully
     */
    bool initiateKeyExchange(NodeNum remoteNode);

    /**
     * Check if we have valid PQ keys for a given node
     * @param remoteNode Node to check
     * @return true if we have valid PQ keys
     */
    bool hasValidPQKeys(NodeNum remoteNode);

    /**
     * Get our current PQ capabilities flags
     */
    uint32_t getPQCapabilities();

    /**
     * Get hash of our current PQ public key
     */
    bool getPQKeyHash(uint8_t hash[32]);

    virtual bool wantUIFrame() { return false; }

  protected:
    /**
     * Handle incoming PQ key exchange messages
     */
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_PQKeyExchange *pqex) override;

    /**
     * Handle admin messages for PQ key management
     */
    virtual AdminMessageHandleResult handleAdminMessageForModule(const meshtastic_MeshPacket &mp,
                                                               meshtastic_AdminMessage *request,
                                                               meshtastic_AdminMessage *response) override;

    /**
     * Allocate reply packet for incoming requests
     */
    virtual meshtastic_MeshPacket *allocReply() override;

  private:
    std::map<uint32_t, PQKeyExchangeSession> activeSessions;
    uint32_t nextSessionId;

    /**
     * Generate a new session ID
     */
    uint32_t generateSessionId();

    /**
     * Create a new session for key exchange
     */
    PQKeyExchangeSession* createSession(NodeNum remoteNode, bool isInitiator);

    /**
     * Find session by session ID
     */
    PQKeyExchangeSession* findSession(uint32_t sessionId);

    /**
     * Clean up expired sessions
     */
    void cleanupExpiredSessions();

    /**
     * Send capability announcement
     */
    bool sendCapabilityAnnouncement(NodeNum remoteNode);

    /**
     * Send key exchange request (initiator)
     */
    bool sendKeyExchangeRequest(NodeNum remoteNode, uint32_t sessionId);

    /**
     * Send fragmented key material
     */
    bool sendKeyFragment(NodeNum remoteNode, uint32_t sessionId, const uint8_t* keyData, 
                        size_t totalSize, uint32_t sequence, uint32_t totalFragments);

    /**
     * Handle capability announcement from remote node
     */
    bool handleCapabilityAnnouncement(const meshtastic_MeshPacket &mp, meshtastic_PQKeyExchange *pqex);

    /**
     * Handle key exchange request
     */
    bool handleKeyExchangeRequest(const meshtastic_MeshPacket &mp, meshtastic_PQKeyExchange *pqex);

    /**
     * Handle key fragment
     */
    bool handleKeyFragment(const meshtastic_MeshPacket &mp, meshtastic_PQKeyExchange *pqex);

    /**
     * Handle key confirmation
     */
    bool handleKeyConfirm(const meshtastic_MeshPacket &mp, meshtastic_PQKeyExchange *pqex);

    /**
     * Complete key exchange and establish session
     */
    bool completeKeyExchange(PQKeyExchangeSession* session);

    /**
     * Verify received key fragments
     */
    bool verifyKeyFragments(PQKeyExchangeSession* session);

    /**
     * Store PQ keys in NodeDB
     */
    bool storePQKeys(NodeNum remoteNode, const uint8_t* publicKey, size_t keySize);
};

extern PQKeyExchangeModule *pqKeyExchangeModule;

#endif // !(MESHTASTIC_EXCLUDE_PQ_CRYPTO)
