/**
 * @file AkitaMeshZmodem.cpp
 * @brief Full implementation of XModem adapter for ZmodemModule
 * @version 2.0.0
 * @date 2025-12-01
 *
 * Integrates with firmware's existing XModem protobuf format for file transfers
 *
 * @copyright Copyright (c) 2025 Akita Engineering
 */

#include "AkitaMeshZmodem.h"
#include "AkitaMeshZmodemConfig.h"
#include "configuration.h"
#include "Router.h"
#include "SPILock.h"
#include "mesh/generated/meshtastic/xmodem.pb.h"
#include <pb_decode.h>
#include <pb_encode.h>

// Transfer timeout in milliseconds (30 seconds)
#define TRANSFER_TIMEOUT_MS 30000

// Maximum retransmit attempts
#define MAX_RETRANS 25

// XModem buffer size (from protobuf definition)
#define XMODEM_BUFFER_SIZE 128

AkitaMeshZmodem::AkitaMeshZmodem()
    : currentState(TransferState::IDLE)
    , remoteNodeId(0)
    , currentFilename("")
    , bytesTransferred(0)
    , totalFileSize(0)
    , lastActivityTime(0)
    , isSender(false)
    , packetSeq(0)
    , retransCount(MAX_RETRANS)
    , isEOT(false)
{
}

AkitaMeshZmodem::~AkitaMeshZmodem()
{
    // Close file if open
    if (activeFile) {
        activeFile.close();
    }
}

void AkitaMeshZmodem::begin(Router* rtr, FS& fs, Print* debugStream)
{
    // Store router reference for sending packets
    routerInstance = rtr;

    LOG_DEBUG("AkitaMeshZmodem: Initialized with router\n");
}

bool AkitaMeshZmodem::startSend(const String& filename, NodeNum destNodeId)
{
    if (currentState != TransferState::IDLE) {
        LOG_ERROR("AkitaMeshZmodem: Cannot start send, not in IDLE state\n");
        return false;
    }

#ifdef FSCom
    // Check if file exists
    if (!FSCom.exists(filename.c_str())) {
        LOG_ERROR("AkitaMeshZmodem: File not found: %s\n", filename.c_str());
        return false;
    }

    // Open file for reading
    if (spiLock) spiLock->lock();
#if defined(ARCH_NRF52) || defined(ARCH_STM32WL)
    activeFile = FSCom.open(filename.c_str(), FILE_O_READ);
#else
    activeFile = FSCom.open(filename.c_str(), "r");
#endif
    if (spiLock) spiLock->unlock();

    if (!activeFile) {
        LOG_ERROR("AkitaMeshZmodem: Failed to open file: %s\n", filename.c_str());
        return false;
    }

    // Get file size
    totalFileSize = activeFile.size();
    bytesTransferred = 0;

    // Save transfer parameters
    currentFilename = filename;
    remoteNodeId = destNodeId;
    isSender = true;
    packetSeq = 0; // Start with filename packet
    isEOT = false;

    // Update state
    currentState = TransferState::SENDING;
    updateActivity();

    LOG_INFO("AkitaMeshZmodem: Started SEND of %s (%d bytes) to node 0x%08x\n",
             filename.c_str(), totalFileSize, destNodeId);

    // Send initial packet (seq=0) with filename
    sendFilenamePacket();

    return true;
#else
    LOG_ERROR("AkitaMeshZmodem: Filesystem not available\n");
    return false;
#endif
}

bool AkitaMeshZmodem::startReceive(const String& filename)
{
    if (currentState != TransferState::IDLE) {
        LOG_ERROR("AkitaMeshZmodem: Cannot start receive, not in IDLE state\n");
        return false;
    }

    // Save parameters - file will be opened when we receive the first packet
    currentFilename = filename;
    isSender = false;
    bytesTransferred = 0;
    totalFileSize = 0;
    packetSeq = 0;

    // Update state
    currentState = TransferState::RECEIVING;
    updateActivity();

    LOG_INFO("AkitaMeshZmodem: Started RECEIVE to %s\n", filename.c_str());

    return true;
}

void AkitaMeshZmodem::processDataPacket(const meshtastic_MeshPacket &packet)
{
    if (currentState != TransferState::RECEIVING) {
        LOG_DEBUG("AkitaMeshZmodem: Ignoring data packet, not in RECEIVING state\n");
        return;
    }

    updateActivity();

    // Decode XModem protobuf from payload
    meshtastic_XModem xmodemPacket = meshtastic_XModem_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(packet.decoded.payload.bytes, packet.decoded.payload.size);

    if (!pb_decode(&stream, &meshtastic_XModem_msg, &xmodemPacket)) {
        LOG_ERROR("AkitaMeshZmodem: Failed to decode XModem packet\n");
        sendControlPacket(meshtastic_XModem_Control_NAK);
        return;
    }

    LOG_DEBUG("AkitaMeshZmodem: Received XModem control=%d, seq=%d, size=%d\n",
              xmodemPacket.control, xmodemPacket.seq, xmodemPacket.buffer.size);

    // Handle packet based on control type
    handleXModemPacket(xmodemPacket);
}

void AkitaMeshZmodem::handleXModemPacket(const meshtastic_XModem &xmodemPacket)
{
    switch (xmodemPacket.control) {
        case meshtastic_XModem_Control_SOH:
        case meshtastic_XModem_Control_STX:
            if (xmodemPacket.seq == 0) {
                // Filename packet - sender initiating transfer
                handleFilenamePacket(xmodemPacket);
            } else {
                // Data packet
                handleDataChunk(xmodemPacket);
            }
            break;

        case meshtastic_XModem_Control_EOT:
            // End of transmission
            handleEndOfTransfer();
            break;

        case meshtastic_XModem_Control_ACK:
            // ACK received (we're the sender)
            if (isSender) {
                handleAckReceived();
            }
            break;

        case meshtastic_XModem_Control_NAK:
            // NAK received (we're the sender)
            if (isSender) {
                handleNakReceived();
            }
            break;

        case meshtastic_XModem_Control_CAN:
            // Cancel transfer
            LOG_WARN("AkitaMeshZmodem: Transfer cancelled by peer\n");
            if (activeFile) activeFile.close();
            currentState = TransferState::ERROR;
            break;

        default:
            LOG_DEBUG("AkitaMeshZmodem: Unknown XModem control: %d\n", xmodemPacket.control);
            break;
    }
}

void AkitaMeshZmodem::handleFilenamePacket(const meshtastic_XModem &xmodemPacket)
{
#ifdef FSCom
    // Open file for writing
    if (spiLock) spiLock->lock();
#if defined(ARCH_NRF52) || defined(ARCH_STM32WL)
    activeFile = FSCom.open(currentFilename.c_str(), FILE_O_WRITE);
#else
    activeFile = FSCom.open(currentFilename.c_str(), "w");
#endif
    if (spiLock) spiLock->unlock();

    if (!activeFile) {
        LOG_ERROR("AkitaMeshZmodem: Failed to open file for writing: %s\n", currentFilename.c_str());
        sendControlPacket(meshtastic_XModem_Control_NAK);
        currentState = TransferState::ERROR;
        return;
    }

    LOG_INFO("AkitaMeshZmodem: File opened for receive, sending ACK\n");
    sendControlPacket(meshtastic_XModem_Control_ACK);
    packetSeq = 1; // Ready for first data packet
#endif
}

void AkitaMeshZmodem::handleDataChunk(const meshtastic_XModem &xmodemPacket)
{
    // Verify sequence number
    if (xmodemPacket.seq != packetSeq) {
        LOG_WARN("AkitaMeshZmodem: Sequence mismatch, expected %d got %d\n", packetSeq, xmodemPacket.seq);
        sendControlPacket(meshtastic_XModem_Control_NAK);
        return;
    }

    // Verify CRC
    unsigned short calculatedCrc = crc16_ccitt(xmodemPacket.buffer.bytes, xmodemPacket.buffer.size);
    if (calculatedCrc != xmodemPacket.crc16) {
        LOG_ERROR("AkitaMeshZmodem: CRC mismatch\n");
        sendControlPacket(meshtastic_XModem_Control_NAK);
        return;
    }

    // Write data to file
    if (activeFile) {
        if (spiLock) spiLock->lock();
        size_t written = activeFile.write(xmodemPacket.buffer.bytes, xmodemPacket.buffer.size);
        if (spiLock) spiLock->unlock();

        bytesTransferred += written;
        packetSeq++;

        LOG_DEBUG("AkitaMeshZmodem: Wrote %d bytes, total %d\n", written, bytesTransferred);

        // Send ACK
        sendControlPacket(meshtastic_XModem_Control_ACK);
    }
}

void AkitaMeshZmodem::handleEndOfTransfer()
{
    LOG_INFO("AkitaMeshZmodem: End of transfer received, %d bytes\n", bytesTransferred);

    // Close file
    if (activeFile) {
        if (spiLock) spiLock->lock();
        activeFile.flush();
        activeFile.close();
        if (spiLock) spiLock->unlock();
    }

    // Send ACK
    sendControlPacket(meshtastic_XModem_Control_ACK);

    // Mark complete
    currentState = TransferState::COMPLETE;
}

void AkitaMeshZmodem::handleAckReceived()
{
    // Sender received ACK, send next packet
    retransCount = MAX_RETRANS; // Reset retry counter

    if (isEOT) {
        // We sent EOT, transfer complete
        LOG_INFO("AkitaMeshZmodem: Transfer complete, %d bytes sent\n", bytesTransferred);
        if (activeFile) {
            if (spiLock) spiLock->lock();
            activeFile.close();
            if (spiLock) spiLock->unlock();
        }
        currentState = TransferState::COMPLETE;
        return;
    }

    // Send next data packet
    packetSeq++;
    sendNextDataPacket();
}

void AkitaMeshZmodem::handleNakReceived()
{
    // Retransmit current packet
    if (--retransCount <= 0) {
        LOG_ERROR("AkitaMeshZmodem: Max retransmit attempts reached\n");
        sendControlPacket(meshtastic_XModem_Control_CAN);
        if (activeFile) activeFile.close();
        currentState = TransferState::ERROR;
        return;
    }

    LOG_DEBUG("AkitaMeshZmodem: NAK received, retransmitting packet %d\n", packetSeq);
    sendNextDataPacket(); // Resend same packet
}

void AkitaMeshZmodem::sendFilenamePacket()
{
    // Send seq=0 packet with filename (STX = request to send)
    meshtastic_XModem xmodemPacket = meshtastic_XModem_init_zero;
    xmodemPacket.control = meshtastic_XModem_Control_STX;
    xmodemPacket.seq = 0;

    // Copy filename to buffer
    size_t nameLen = currentFilename.length();
    if (nameLen > sizeof(xmodemPacket.buffer.bytes)) {
        nameLen = sizeof(xmodemPacket.buffer.bytes);
    }
    memcpy(xmodemPacket.buffer.bytes, currentFilename.c_str(), nameLen);
    xmodemPacket.buffer.size = nameLen;
    xmodemPacket.crc16 = crc16_ccitt(xmodemPacket.buffer.bytes, xmodemPacket.buffer.size);

    LOG_INFO("AkitaMeshZmodem: Sending filename packet for %s\n", currentFilename.c_str());
    sendXModemPacket(xmodemPacket);
}

void AkitaMeshZmodem::sendNextDataPacket()
{
    if (!activeFile) {
        LOG_ERROR("AkitaMeshZmodem: File not open for sending\n");
        currentState = TransferState::ERROR;
        return;
    }

    // Read next chunk from file
    meshtastic_XModem xmodemPacket = meshtastic_XModem_init_zero;
    xmodemPacket.control = meshtastic_XModem_Control_SOH;
    xmodemPacket.seq = packetSeq;

    // Read data
    if (spiLock) spiLock->lock();
    xmodemPacket.buffer.size = activeFile.read(xmodemPacket.buffer.bytes, XMODEM_BUFFER_SIZE);
    if (spiLock) spiLock->unlock();

    // Calculate CRC
    xmodemPacket.crc16 = crc16_ccitt(xmodemPacket.buffer.bytes, xmodemPacket.buffer.size);

    // Check if this is the last packet
    if (xmodemPacket.buffer.size < XMODEM_BUFFER_SIZE) {
        isEOT = true;
        LOG_DEBUG("AkitaMeshZmodem: Last packet (size=%d), will send EOT after ACK\n", xmodemPacket.buffer.size);
    }

    bytesTransferred += xmodemPacket.buffer.size;

    LOG_DEBUG("AkitaMeshZmodem: Sending packet %d (%d bytes, total %d/%d)\n",
              packetSeq, xmodemPacket.buffer.size, bytesTransferred, totalFileSize);

    sendXModemPacket(xmodemPacket);
}

void AkitaMeshZmodem::sendControlPacket(meshtastic_XModem_Control control)
{
    meshtastic_XModem xmodemPacket = meshtastic_XModem_init_zero;
    xmodemPacket.control = control;
    xmodemPacket.seq = packetSeq;

    LOG_DEBUG("AkitaMeshZmodem: Sending control packet: %d\n", control);
    sendXModemPacket(xmodemPacket);

    // If we're sending EOT, mark it
    if (control == meshtastic_XModem_Control_EOT) {
        isEOT = true;
    }
}

void AkitaMeshZmodem::sendXModemPacket(const meshtastic_XModem &xmodemPacket)
{
    if (!routerInstance) {
        LOG_ERROR("AkitaMeshZmodem: Router not initialized\n");
        return;
    }

    // Allocate mesh packet
    meshtastic_MeshPacket *packet = routerInstance->allocForSending();
    if (!packet) {
        LOG_ERROR("AkitaMeshZmodem: Failed to allocate packet\n");
        return;
    }

    // Encode XModem protobuf to payload
    pb_ostream_t stream = pb_ostream_from_buffer(packet->decoded.payload.bytes,
                                                  sizeof(packet->decoded.payload.bytes));

    if (!pb_encode(&stream, &meshtastic_XModem_msg, &xmodemPacket)) {
        LOG_ERROR("AkitaMeshZmodem: Failed to encode XModem packet\n");
        return;
    }

    // Set packet parameters
    packet->to = remoteNodeId;
    packet->decoded.portnum = (meshtastic_PortNum)AKZ_ZMODEM_DATA_PORTNUM;
    packet->decoded.payload.size = stream.bytes_written;
    packet->want_ack = true; // Request Meshtastic layer ACK

    // Send packet
    routerInstance->enqueueReceivedMessage(packet);

    LOG_DEBUG("AkitaMeshZmodem: Sent XModem packet (%d bytes encoded)\n", stream.bytes_written);
}

unsigned short AkitaMeshZmodem::crc16_ccitt(const uint8_t *buffer, int length)
{
    unsigned short crc16 = 0;
    while (length != 0) {
        crc16 = (unsigned char)(crc16 >> 8) | (crc16 << 8);
        crc16 ^= *buffer;
        crc16 ^= (unsigned char)(crc16 & 0xff) >> 4;
        crc16 ^= (crc16 << 8) << 4;
        crc16 ^= ((crc16 & 0xff) << 4) << 1;
        buffer++;
        length--;
    }
    return crc16;
}

AkitaMeshZmodem::TransferState AkitaMeshZmodem::loop()
{
    switch (currentState) {
        case TransferState::IDLE:
            // Nothing to do
            break;

        case TransferState::SENDING:
            // Sending is event-driven (ACK/NAK responses)
            // Check for timeout
            if (hasTimedOut()) {
                LOG_ERROR("AkitaMeshZmodem: Send transfer timed out\n");
                if (activeFile) activeFile.close();
                currentState = TransferState::ERROR;
            }
            break;

        case TransferState::RECEIVING:
            // Receiving is event-driven (processDataPacket)
            // Check for timeout
            if (hasTimedOut()) {
                LOG_ERROR("AkitaMeshZmodem: Receive transfer timed out\n");
                if (activeFile) activeFile.close();
                currentState = TransferState::ERROR;
            }
            break;

        case TransferState::COMPLETE:
        case TransferState::ERROR:
            // Terminal states
            break;
    }

    return currentState;
}

void AkitaMeshZmodem::updateActivity()
{
    lastActivityTime = millis();
}

bool AkitaMeshZmodem::hasTimedOut() const
{
    return (millis() - lastActivityTime) > TRANSFER_TIMEOUT_MS;
}
