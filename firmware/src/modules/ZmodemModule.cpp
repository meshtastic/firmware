/**
 * @file ZmodemModule.cpp
 * @author Akita Engineering
 * @brief Implementation of Meshtastic ZModem Module with multi-transfer support
 * @version 2.0.0
 * @date 2025-12-01
 *
 * @copyright Copyright (c) 2025 Akita Engineering
 */

#include "ZmodemModule.h"
#include "AkitaMeshZmodemConfig.h"
#include "configuration.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

// Logging support
#include "FSCommon.h"

// Global instance
ZmodemModule *zmodemModule;

// --- TransferSession Implementation ---

TransferSession::TransferSession(uint32_t id, NodeNum nodeId, const String& fname, TransferDirection dir)
    : sessionId(id)
    , remoteNodeId(nodeId)
    , filename(fname)
    , direction(dir)
    , state(AkitaMeshZmodem::TransferState::IDLE)
    , bytesTransferred(0)
    , totalSize(0)
    , lastActivity(millis())
    , zmodemInstance(nullptr)
{
    // Create a new AkitaMeshZmodem instance for this session
    zmodemInstance = new AkitaMeshZmodem();
}

TransferSession::~TransferSession()
{
    // Clean up zmodem instance
    if (zmodemInstance) {
        delete zmodemInstance;
        zmodemInstance = nullptr;
    }
}

bool TransferSession::isTimedOut() const
{
    return (millis() - lastActivity) > TRANSFER_SESSION_TIMEOUT_MS;
}

void TransferSession::updateActivity()
{
    lastActivity = millis();
}

// --- ZmodemModule Implementation ---

ZmodemModule::ZmodemModule()
    : MeshModule("ZmodemModule")
    , nextSessionId(1)
{
    LOG_INFO("Initializing ZmodemModule v2.0.0...\n");
    LOG_INFO("  Max concurrent transfers: %d\n", MAX_CONCURRENT_TRANSFERS);
    LOG_INFO("  Command port: %d\n", AKZ_ZMODEM_COMMAND_PORTNUM);
    LOG_INFO("  Data port: %d\n", AKZ_ZMODEM_DATA_PORTNUM);
    LOG_INFO("  Session timeout: %d ms\n", TRANSFER_SESSION_TIMEOUT_MS);

    // Check filesystem availability
#ifdef FSCom
    if (!FSCom.exists("/")) {
        LOG_ERROR("ZmodemModule: Filesystem not available! Module may not function correctly.\n");
    }
#endif

    LOG_INFO("ZmodemModule initialized successfully.\n");
}

ZmodemModule::~ZmodemModule()
{
    // Clean up all active sessions
    for (auto session : activeSessions) {
        delete session;
    }
    activeSessions.clear();
}

// Note: Initialization moved to constructor per Meshtastic module pattern

void ZmodemModule::loop()
{
    // Process all active sessions
    for (size_t i = 0; i < activeSessions.size(); ) {
        TransferSession* session = activeSessions[i];

        // Update session state by calling library loop
        if (session->zmodemInstance) {
            AkitaMeshZmodem::TransferState currentState = session->zmodemInstance->loop();
            session->state = currentState;

            // Update progress tracking
            session->bytesTransferred = session->zmodemInstance->getBytesTransferred();
            session->totalSize = session->zmodemInstance->getTotalFileSize();

            // Check for completion or error
            if (currentState == AkitaMeshZmodem::TransferState::COMPLETE) {
                LOG_INFO("Session %d: Transfer COMPLETE (%d bytes)\n",
                         session->sessionId,
                         session->bytesTransferred);
                removeSession(session);
                continue; // Don't increment i, element was removed
            }
            else if (currentState == AkitaMeshZmodem::TransferState::ERROR) {
                LOG_ERROR("Session %d: Transfer ERROR\n", session->sessionId);
                removeSession(session);
                continue; // Don't increment i, element was removed
            }
        }

        i++; // Move to next session
    }

    // Periodic cleanup of stale sessions
    static unsigned long lastCleanup = 0;
    if (millis() - lastCleanup > 10000) { // Every 10 seconds
        cleanupStaleSessions();
        lastCleanup = millis();
    }

    // Periodic status logging
    static unsigned long lastStatus = 0;
    if (millis() - lastStatus > 30000 && activeSessions.size() > 0) { // Every 30 seconds
        logSessionStats();
        lastStatus = millis();
    }
}

bool ZmodemModule::wantPacket(const meshtastic_MeshPacket *p)
{
    // Accept packets on either our command port or data port
    return (p->decoded.portnum == AKZ_ZMODEM_COMMAND_PORTNUM ||
            p->decoded.portnum == AKZ_ZMODEM_DATA_PORTNUM);
}

ProcessMessage ZmodemModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    // Route to appropriate handler based on port
    if (mp.decoded.portnum == AKZ_ZMODEM_COMMAND_PORTNUM) {
        return handleCommandPacket(mp);
    }
    else if (mp.decoded.portnum == AKZ_ZMODEM_DATA_PORTNUM) {
        return handleDataPacket(mp);
    }

    return ProcessMessage::CONTINUE; // Not for us
}

ProcessMessage ZmodemModule::handleCommandPacket(const meshtastic_MeshPacket &mp)
{
    LOG_DEBUG("ZmodemModule: Received command packet from 0x%08x\n", mp.from);

    // Extract payload as string
    size_t payloadLen = mp.decoded.payload.size;
    if (payloadLen == 0 || payloadLen > 200) {
        LOG_WARN("ZmodemModule: Invalid command payload length: %d\n", payloadLen);
        return ProcessMessage::CONTINUE;
    }

    // Convert payload to String
    char cmdBuffer[201];
    memcpy(cmdBuffer, mp.decoded.payload.bytes, payloadLen);
    cmdBuffer[payloadLen] = '\0';
    String command(cmdBuffer);

    LOG_INFO("ZmodemModule: Command '%s' from 0x%08x\n", command.c_str(), mp.from);

    // Handle the command
    handleCommand(command, mp.from);

    return ProcessMessage::STOP; // We handled this packet
}

ProcessMessage ZmodemModule::handleDataPacket(const meshtastic_MeshPacket &mp)
{
    LOG_DEBUG("ZmodemModule: Received data packet from 0x%08x\n", mp.from);

    // Find the session for this node
    TransferSession* session = findSession(mp.from);

    if (!session) {
        LOG_DEBUG("ZmodemModule: No active session for node 0x%08x, ignoring data packet\n", mp.from);
        return ProcessMessage::CONTINUE;
    }

    // Process packets for both SENDING (ACK/NAK) and RECEIVING (data) states
    if (session->state != AkitaMeshZmodem::TransferState::SENDING &&
        session->state != AkitaMeshZmodem::TransferState::RECEIVING) {
        LOG_DEBUG("ZmodemModule: Session %d not in active state (state=%d), ignoring\n",
                  session->sessionId, (int)session->state);
        return ProcessMessage::CONTINUE;
    }

    // Update activity timestamp
    session->updateActivity();

    // Feed packet to the zmodem library (handles both send and receive)
    if (session->zmodemInstance) {
        session->zmodemInstance->processDataPacket(mp);
    }

    return ProcessMessage::STOP; // We consumed this packet
}

void ZmodemModule::handleCommand(const String& msg, NodeNum fromNodeId)
{
    String command;
    String args;

    // Parse command format
    if (msg.startsWith("SEND:")) {
        command = "SEND";
        args = msg.substring(5); // e.g., "!NodeID:/path/file.txt"
    }
    else if (msg.startsWith("RECV:")) {
        command = "RECV";
        args = msg.substring(5); // e.g., "/path/file.txt"
    }
    else {
        LOG_WARN("ZmodemModule: Unknown command '%s'\n", msg.c_str());
        sendReply("ERROR: Unknown command: " + msg, fromNodeId);
        return;
    }

    // Check if we can accept new transfers
    if (!canAcceptNewTransfer()) {
        LOG_WARN("ZmodemModule: At max concurrent transfer limit (%d)\n", MAX_CONCURRENT_TRANSFERS);
        sendReply("ERROR: Maximum concurrent transfers reached. Try again later.", fromNodeId);
        return;
    }

    // --- RECV Command ---
    if (command == "RECV") {
        String filename = args;

        // Validate filename
        if (filename.length() == 0 || !filename.startsWith("/")) {
            LOG_ERROR("ZmodemModule: Invalid RECV filename: '%s'\n", filename.c_str());
            sendReply("ERROR: Invalid RECV format. Use RECV:/path/to/save.txt", fromNodeId);
            return;
        }

        // Check if we already have a session with this node
        if (findSession(fromNodeId)) {
            LOG_WARN("ZmodemModule: Already have active session with node 0x%08x\n", fromNodeId);
            sendReply("ERROR: Transfer already in progress with your node", fromNodeId);
            return;
        }

        // Create new session
        TransferSession* session = createSession(fromNodeId, filename, TransferDirection::RECEIVE);
        if (!session) {
            LOG_ERROR("ZmodemModule: Failed to create RECV session\n");
            sendReply("ERROR: Failed to create transfer session", fromNodeId);
            return;
        }

        // Initialize zmodem instance for this session
        if (session->zmodemInstance) {
            session->zmodemInstance->begin(router, FSCom, &Serial);

            // Start receive
            bool success = session->zmodemInstance->startReceive(filename);
            if (success) {
                LOG_INFO("Session %d: Started RECV to '%s'\n", session->sessionId, filename.c_str());
                sendReply("OK: Started RECV to " + filename + ". Waiting for sender...", fromNodeId);
                session->updateActivity();
            }
            else {
                LOG_ERROR("Session %d: startReceive() failed\n", session->sessionId);
                sendReply("ERROR: Failed to start RECV to " + filename, fromNodeId);
                removeSession(session);
            }
        }
    }

    // --- SEND Command ---
    else if (command == "SEND") {
        // Format: SEND:!NodeID:/path/file.txt
        int idEnd = args.indexOf(':');
        if (idEnd <= 0) {
            LOG_ERROR("ZmodemModule: Invalid SEND format (no ':' separator): '%s'\n", args.c_str());
            sendReply("ERROR: Invalid SEND format. Use SEND:!NodeID:/path/file.txt", fromNodeId);
            return;
        }

        String nodeIdStr = args.substring(0, idEnd);
        String filename = args.substring(idEnd + 1);

        // Validate filename
        if (filename.length() == 0 || !filename.startsWith("/")) {
            LOG_ERROR("ZmodemModule: Invalid SEND filename: '%s'\n", filename.c_str());
            sendReply("ERROR: Invalid filename format. Must start with '/'", fromNodeId);
            return;
        }

        // Parse destination NodeID
        // TODO: parseNodeId may need to be replaced with Meshtastic equivalent
        // NodeNum destNodeId = parseNodeId(nodeIdStr.c_str());
        NodeNum destNodeId = 0; // Placeholder - need proper parsing

        // Simple parsing for hex format: !12345678
        if (nodeIdStr.startsWith("!")) {
            destNodeId = strtoul(nodeIdStr.c_str() + 1, nullptr, 16);
        }

        if (destNodeId == 0 || destNodeId == NODENUM_BROADCAST) {
            LOG_ERROR("ZmodemModule: Invalid destination NodeID: '%s'\n", nodeIdStr.c_str());
            sendReply("ERROR: Invalid destination NodeID: " + nodeIdStr, fromNodeId);
            return;
        }

        // Check if we already have a session with this node
        if (findSession(destNodeId)) {
            LOG_WARN("ZmodemModule: Already have active session with node 0x%08x\n", destNodeId);
            sendReply("ERROR: Transfer already in progress with destination node", fromNodeId);
            return;
        }

        // Create new session
        TransferSession* session = createSession(destNodeId, filename, TransferDirection::SEND);
        if (!session) {
            LOG_ERROR("ZmodemModule: Failed to create SEND session\n");
            sendReply("ERROR: Failed to create transfer session", fromNodeId);
            return;
        }

        // Initialize zmodem instance for this session
        if (session->zmodemInstance) {
            session->zmodemInstance->begin(router, FSCom, &Serial);

            // Start send
            bool success = session->zmodemInstance->startSend(filename, destNodeId);
            if (success) {
                LOG_INFO("Session %d: Started SEND of '%s' to 0x%08x\n",
                         session->sessionId, filename.c_str(), destNodeId);
                sendReply("OK: Started SEND of " + filename + " to " + nodeIdStr, fromNodeId);
                session->updateActivity();
            }
            else {
                LOG_ERROR("Session %d: startSend() failed\n", session->sessionId);
                sendReply("ERROR: Failed to start SEND of " + filename, fromNodeId);
                removeSession(session);
            }
        }
    }
}

void ZmodemModule::sendReply(const String& message, NodeNum destinationNodeId, bool wantAck)
{
    LOG_DEBUG("ZmodemModule: Sending reply to 0x%08x: %s\n", destinationNodeId, message.c_str());

    // Allocate packet using router
    meshtastic_MeshPacket *packet = router->allocForSending();
    if (!packet) {
        LOG_ERROR("ZmodemModule: Failed to allocate packet for reply\n");
        return;
    }

    // Set packet fields
    packet->to = destinationNodeId;
    packet->decoded.portnum = (meshtastic_PortNum)AKZ_ZMODEM_COMMAND_PORTNUM;
    packet->want_ack = wantAck;

    // Set payload
    size_t len = message.length();
    if (len > sizeof(packet->decoded.payload.bytes)) {
        len = sizeof(packet->decoded.payload.bytes);
    }
    memcpy(packet->decoded.payload.bytes, message.c_str(), len);
    packet->decoded.payload.size = len;

    // Send via router
    router->enqueueReceivedMessage(packet);
}

// --- Session Management ---

TransferSession* ZmodemModule::findSession(NodeNum nodeId)
{
    for (auto session : activeSessions) {
        if (session->remoteNodeId == nodeId) {
            return session;
        }
    }
    return nullptr;
}

TransferSession* ZmodemModule::findSessionById(uint32_t sessionId)
{
    for (auto session : activeSessions) {
        if (session->sessionId == sessionId) {
            return session;
        }
    }
    return nullptr;
}

TransferSession* ZmodemModule::createSession(NodeNum nodeId, const String& filename, TransferDirection direction)
{
    // Check limit
    if (activeSessions.size() >= MAX_CONCURRENT_TRANSFERS) {
        LOG_ERROR("ZmodemModule: Cannot create session, at max limit\n");
        return nullptr;
    }

    // Create session
    uint32_t sessionId = nextSessionId++;
    TransferSession* session = new TransferSession(sessionId, nodeId, filename, direction);

    if (!session) {
        LOG_ERROR("ZmodemModule: Failed to allocate TransferSession\n");
        return nullptr;
    }

    // Add to active list
    activeSessions.push_back(session);

    LOG_INFO("Created session %d: %s '%s' with node 0x%08x (total sessions: %d)\n",
             sessionId,
             direction == TransferDirection::SEND ? "SEND" : "RECV",
             filename.c_str(),
             nodeId,
             activeSessions.size());

    return session;
}

void ZmodemModule::removeSession(uint32_t sessionId)
{
    TransferSession* session = findSessionById(sessionId);
    if (session) {
        removeSession(session);
    }
}

void ZmodemModule::removeSession(TransferSession* session)
{
    if (!session) return;

    LOG_INFO("Removing session %d (node 0x%08x, %s)\n",
             session->sessionId,
             session->remoteNodeId,
             session->direction == TransferDirection::SEND ? "SEND" : "RECV");

    // Remove from vector
    auto it = std::find(activeSessions.begin(), activeSessions.end(), session);
    if (it != activeSessions.end()) {
        activeSessions.erase(it);
    }

    // Delete session (calls destructor which cleans up zmodem instance)
    delete session;

    LOG_DEBUG("Session removed. Remaining sessions: %d\n", activeSessions.size());
}

void ZmodemModule::cleanupStaleSessions()
{
    for (size_t i = 0; i < activeSessions.size(); ) {
        TransferSession* session = activeSessions[i];

        if (session->isTimedOut()) {
            LOG_WARN("Session %d timed out (no activity for %d ms)\n",
                     session->sessionId, TRANSFER_SESSION_TIMEOUT_MS);
            removeSession(session);
            // Don't increment i, element was removed
        }
        else {
            i++;
        }
    }
}

void ZmodemModule::logSessionStats()
{
    LOG_INFO("=== ZmodemModule Status ===\n");
    LOG_INFO("Active sessions: %d / %d\n", activeSessions.size(), MAX_CONCURRENT_TRANSFERS);

    for (auto session : activeSessions) {
        const char* dirStr = session->direction == TransferDirection::SEND ? "SEND" : "RECV";
        const char* stateStr = "UNKNOWN";

        switch (session->state) {
            case AkitaMeshZmodem::TransferState::IDLE: stateStr = "IDLE"; break;
            case AkitaMeshZmodem::TransferState::SENDING: stateStr = "SENDING"; break;
            case AkitaMeshZmodem::TransferState::RECEIVING: stateStr = "RECEIVING"; break;
            case AkitaMeshZmodem::TransferState::COMPLETE: stateStr = "COMPLETE"; break;
            case AkitaMeshZmodem::TransferState::ERROR: stateStr = "ERROR"; break;
        }

        unsigned long idleTime = millis() - session->lastActivity;
        float progress = 0.0f;
        if (session->totalSize > 0) {
            progress = (float)session->bytesTransferred / session->totalSize * 100.0f;
        }

        LOG_INFO("  Session %d: %s | %s | Node 0x%08x | %s | %d/%d bytes (%.1f%%) | Idle: %lu ms\n",
                 session->sessionId,
                 dirStr,
                 stateStr,
                 session->remoteNodeId,
                 session->filename.c_str(),
                 session->bytesTransferred,
                 session->totalSize,
                 progress,
                 idleTime);
    }
    LOG_INFO("===========================\n");
}
