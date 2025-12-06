/**
 * @file AkitaMeshZmodem.h
 * @brief Adapter to integrate existing XModem implementation with ZmodemModule
 * @version 1.0.0
 * @date 2025-12-01
 *
 * This file provides a compatibility layer between the ZmodemModule's expected
 * AkitaMeshZmodem interface and the existing XModemAdapter implementation in
 * the Meshtastic firmware.
 *
 * @copyright Copyright (c) 2025 Akita Engineering
 */

#pragma once

#include "xmodem.h"
#include "Router.h"
#include "FSCommon.h"

/**
 * @brief Adapter class that wraps XModemAdapter for ZmodemModule compatibility
 *
 * This class provides the interface expected by ZmodemModule while using
 * the existing XModemAdapter implementation for actual file transfer protocol.
 */
class AkitaMeshZmodem {
public:
    /**
     * @brief Transfer state enum matching ZmodemModule expectations
     */
    enum class TransferState {
        IDLE,
        SENDING,
        RECEIVING,
        COMPLETE,
        ERROR
    };

    /**
     * @brief Constructor
     */
    AkitaMeshZmodem();

    /**
     * @brief Destructor
     */
    ~AkitaMeshZmodem();

    /**
     * @brief Initialize the adapter (compatibility method)
     * @param router Router instance (unused, for API compatibility)
     * @param fs Filesystem instance (unused, uses global FSCom)
     * @param debugStream Debug stream (unused, uses LOG macros)
     */
    void begin(Router* router, FS& fs, Print* debugStream);

    /**
     * @brief Start sending a file
     * @param filename Absolute path to file
     * @param destNodeId Destination node ID
     * @return true if send started successfully
     */
    bool startSend(const String& filename, NodeNum destNodeId);

    /**
     * @brief Start receiving a file
     * @param filename Absolute path to save received file
     * @return true if receive started successfully
     */
    bool startReceive(const String& filename);

    /**
     * @brief Process incoming data packet
     * @param packet Mesh packet with file data
     */
    void processDataPacket(const meshtastic_MeshPacket &packet);

    /**
     * @brief Main loop function - process transfer state machine
     * @return Current transfer state
     */
    TransferState loop();

    /**
     * @brief Get current transfer state
     * @return Current state
     */
    TransferState getCurrentState() const { return currentState; }

    /**
     * @brief Get bytes transferred so far
     * @return Number of bytes transferred
     */
    uint32_t getBytesTransferred() const { return bytesTransferred; }

    /**
     * @brief Get total file size
     * @return Total file size in bytes
     */
    uint32_t getTotalFileSize() const { return totalFileSize; }

private:
    TransferState currentState;
    NodeNum remoteNodeId;
    String currentFilename;
    uint32_t bytesTransferred;
    uint32_t totalFileSize;
    unsigned long lastActivityTime;

    // Internal state tracking
    bool isSender;
    File activeFile;
    Router* routerInstance;

    // XModem protocol state
    uint16_t packetSeq;
    int retransCount;
    bool isEOT;

    /**
     * @brief Update activity timestamp
     */
    void updateActivity();

    /**
     * @brief Check if transfer has timed out
     * @return true if timeout occurred
     */
    bool hasTimedOut() const;

    /**
     * @brief Handle incoming XModem packet
     */
    void handleXModemPacket(const meshtastic_XModem &xmodemPacket);

    /**
     * @brief Handle filename packet (seq=0)
     */
    void handleFilenamePacket(const meshtastic_XModem &xmodemPacket);

    /**
     * @brief Handle data chunk packet
     */
    void handleDataChunk(const meshtastic_XModem &xmodemPacket);

    /**
     * @brief Handle end of transfer
     */
    void handleEndOfTransfer();

    /**
     * @brief Handle ACK received (for sender)
     */
    void handleAckReceived();

    /**
     * @brief Handle NAK received (for sender)
     */
    void handleNakReceived();

    /**
     * @brief Send filename packet to initiate transfer
     */
    void sendFilenamePacket();

    /**
     * @brief Send next data packet
     */
    void sendNextDataPacket();

    /**
     * @brief Send control packet (ACK, NAK, EOT, etc.)
     */
    void sendControlPacket(meshtastic_XModem_Control control);

    /**
     * @brief Send XModem packet via mesh
     */
    void sendXModemPacket(const meshtastic_XModem &xmodemPacket);

    /**
     * @brief Calculate CRC16-CCITT checksum
     */
    unsigned short crc16_ccitt(const uint8_t *buffer, int length);
};
