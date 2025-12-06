/**
 * @file ZmodemModule.h
 * @author Akita Engineering
 * @brief Meshtastic Module for handling ZModem file transfers with multi-transfer support
 * @version 2.0.0
 * @date 2025-12-01
 *
 * @copyright Copyright (c) 2025 Akita Engineering
 *
 * Major changes in v2.0.0:
 * - Support for multiple concurrent file transfers
 * - Updated to use MeshModule base class (Meshtastic firmware pattern)
 * - Session-based transfer management
 * - Improved reliability and ACK handling
 */

#pragma once

#include "MeshModule.h"
#include "Router.h"
#include <AkitaMeshZmodem.h>
#include "AkitaMeshZmodemConfig.h"
#include <vector>

// Maximum number of concurrent file transfers
#ifndef MAX_CONCURRENT_TRANSFERS
#define MAX_CONCURRENT_TRANSFERS 5
#endif

// Session timeout in milliseconds (60 seconds of inactivity)
#ifndef TRANSFER_SESSION_TIMEOUT_MS
#define TRANSFER_SESSION_TIMEOUT_MS 60000
#endif

/**
 * @brief Direction of file transfer
 */
enum class TransferDirection {
    SEND,    // Sending file to remote node
    RECEIVE  // Receiving file from remote node
};

/**
 * @brief Represents an active file transfer session
 *
 * Each session tracks one file transfer (send or receive) with a specific node.
 * Multiple sessions can be active concurrently.
 */
struct TransferSession {
    uint32_t sessionId;                      // Unique session identifier
    NodeNum remoteNodeId;                    // Remote node involved in transfer
    String filename;                         // File path being transferred
    TransferDirection direction;             // SEND or RECEIVE
    AkitaMeshZmodem::TransferState state;   // Current transfer state
    uint32_t bytesTransferred;               // Progress tracking
    uint32_t totalSize;                      // Total file size
    unsigned long lastActivity;              // Last packet time (for timeout)
    AkitaMeshZmodem* zmodemInstance;        // Per-session protocol handler

    /**
     * @brief Constructor for TransferSession
     */
    TransferSession(uint32_t id, NodeNum nodeId, const String& fname, TransferDirection dir);

    /**
     * @brief Destructor - cleans up zmodem instance
     */
    ~TransferSession();

    /**
     * @brief Check if session has timed out
     * @return true if no activity for TRANSFER_SESSION_TIMEOUT_MS
     */
    bool isTimedOut() const;

    /**
     * @brief Update last activity timestamp
     */
    void updateActivity();
};

/**
 * @brief Meshtastic Module for ZModem file transfers with multi-transfer support
 *
 * Features:
 * - Multiple concurrent file transfers (configurable limit)
 * - Session-based management
 * - Dual port operation (command: 250, data: 251)
 * - Automatic timeout and cleanup
 * - Compatible with Meshtastic firmware patterns
 */
class ZmodemModule : public MeshModule {
public:
    /**
     * @brief Constructor
     */
    ZmodemModule();

    /**
     * @brief Destructor - cleanup all sessions
     */
    virtual ~ZmodemModule();

    /**
     * @brief Module loop function, called repeatedly
     *
     * Processes all active transfer sessions and performs cleanup
     */
    virtual void loop();

protected:
    /**
     * @brief Check if this module wants to process the packet
     * @param p Pointer to the mesh packet
     * @return true if packet is for our command (250) or data (251) port
     */
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override;

    /**
     * @brief Handle received packets for this module
     * @param mp Reference to the received mesh packet
     * @param src Source of the packet (radio, serial, etc.)
     * @return ProcessMessage::STOP if packet was handled, CONTINUE otherwise
     */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

private:
    // Session management
    std::vector<TransferSession*> activeSessions;
    uint32_t nextSessionId;

    /**
     * @brief Find an active session by remote node ID
     * @param nodeId Node ID to search for
     * @return Pointer to session or nullptr if not found
     */
    TransferSession* findSession(NodeNum nodeId);

    /**
     * @brief Find an active session by session ID
     * @param sessionId Session ID to search for
     * @return Pointer to session or nullptr if not found
     */
    TransferSession* findSessionById(uint32_t sessionId);

    /**
     * @brief Create a new transfer session
     * @param nodeId Remote node ID
     * @param filename File path
     * @param direction Transfer direction (SEND or RECEIVE)
     * @return Pointer to new session or nullptr if failed/limit reached
     */
    TransferSession* createSession(NodeNum nodeId, const String& filename, TransferDirection direction);

    /**
     * @brief Remove and cleanup a session
     * @param sessionId Session ID to remove
     */
    void removeSession(uint32_t sessionId);

    /**
     * @brief Remove and cleanup a session by pointer
     * @param session Pointer to session to remove
     */
    void removeSession(TransferSession* session);

    /**
     * @brief Clean up timed out or completed sessions
     */
    void cleanupStaleSessions();

    /**
     * @brief Get count of active sessions
     * @return Number of active transfer sessions
     */
    size_t getActiveSessionCount() const { return activeSessions.size(); }

    /**
     * @brief Check if we can accept a new transfer
     * @return true if under MAX_CONCURRENT_TRANSFERS limit
     */
    bool canAcceptNewTransfer() const { return activeSessions.size() < MAX_CONCURRENT_TRANSFERS; }

    // Command handling

    /**
     * @brief Parse and handle incoming text commands
     * @param msg Command string (e.g., "SEND:!NodeID:/path" or "RECV:/path")
     * @param fromNodeId Node ID of command sender
     */
    void handleCommand(const String& msg, NodeNum fromNodeId);

    /**
     * @brief Send a text reply message back to a node
     * @param message Reply text
     * @param destinationNodeId Destination node
     * @param wantAck Whether to request acknowledgment
     */
    void sendReply(const String& message, NodeNum destinationNodeId, bool wantAck = false);

    // Packet handling

    /**
     * @brief Handle command packet (port 250)
     * @param mp Mesh packet with command
     * @return ProcessMessage::STOP if handled
     */
    ProcessMessage handleCommandPacket(const meshtastic_MeshPacket &mp);

    /**
     * @brief Handle data packet (port 251)
     * @param mp Mesh packet with zmodem data
     * @return ProcessMessage::STOP if handled
     */
    ProcessMessage handleDataPacket(const meshtastic_MeshPacket &mp);

    // Utility

    /**
     * @brief Log session statistics
     */
    void logSessionStats();
};

// Global instance pointer (defined in .cpp)
extern ZmodemModule *zmodemModule;
