/***********************************************************************************************************************
 * based on XMODEM implementation by Georges Menie (www.menie.org)
 ***********************************************************************************************************************
 * Copyright 2001-2019 Georges Menie (www.menie.org)
 * All rights reserved.
 *
 * Adapted for protobuf encapsulation. this is not really Xmodem any more.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the University of California, Berkeley nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS AND CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **********************************************************************************************************************/

#include "xmodem.h"

XModemAdapter xModem;

XModemAdapter::XModemAdapter() {}

unsigned short XModemAdapter::crc16_ccitt(const pb_byte_t *buffer, int length)
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

int XModemAdapter::check(const pb_byte_t *buf, int sz, unsigned short tcrc)
{
    return crc16_ccitt(buf, sz) == tcrc;
}

void XModemAdapter::sendControl(meshtastic_XModem_Control c)
{
    xmodemStore = meshtastic_XModem_init_zero;
    xmodemStore.control = c;
    LOG_DEBUG("XModem: Notify Sending control %d.\n", c);
    packetReady.notifyObservers(packetno);
}

meshtastic_XModem XModemAdapter::getForPhone()
{
    return xmodemStore;
}

void XModemAdapter::resetForPhone()
{
    xmodemStore = meshtastic_XModem_init_zero;
}

void XModemAdapter::handlePacket(meshtastic_XModem xmodemPacket)
{
    switch (xmodemPacket.control) {
    case meshtastic_XModem_Control_SOH:
    case meshtastic_XModem_Control_STX:
        if ((xmodemPacket.seq == 0) && !isReceiving && !isTransmitting) {
            // NULL packet has the destination filename
            memcpy(filename, &xmodemPacket.buffer.bytes, xmodemPacket.buffer.size);
            if (xmodemPacket.control == meshtastic_XModem_Control_SOH) { // Receive this file and put to Flash
                file = FSCom.open(filename, FILE_O_WRITE);
                if (file) {
                    sendControl(meshtastic_XModem_Control_ACK);
                    isReceiving = true;
                    packetno = 1;
                    break;
                }
                sendControl(meshtastic_XModem_Control_NAK);
                isReceiving = false;
                break;
            } else { // Transmit this file from Flash
                LOG_INFO("XModem: Transmitting file %s\n", filename);
                file = FSCom.open(filename, FILE_O_READ);
                if (file) {
                    packetno = 1;
                    isTransmitting = true;
                    xmodemStore = meshtastic_XModem_init_zero;
                    xmodemStore.control = meshtastic_XModem_Control_SOH;
                    xmodemStore.seq = packetno;
                    xmodemStore.buffer.size = file.read(xmodemStore.buffer.bytes, sizeof(meshtastic_XModem_buffer_t::bytes));
                    xmodemStore.crc16 = crc16_ccitt(xmodemStore.buffer.bytes, xmodemStore.buffer.size);
                    LOG_DEBUG("XModem: STX Notify Sending packet %d, %d Bytes.\n", packetno, xmodemStore.buffer.size);
                    if (xmodemStore.buffer.size < sizeof(meshtastic_XModem_buffer_t::bytes)) {
                        isEOT = true;
                        // send EOT on next Ack
                    }
                    packetReady.notifyObservers(packetno);
                    break;
                }
                sendControl(meshtastic_XModem_Control_NAK);
                isTransmitting = false;
                break;
            }
        } else {
            if (isReceiving) {
                // normal file data packet
                if ((xmodemPacket.seq == packetno) &&
                    check(xmodemPacket.buffer.bytes, xmodemPacket.buffer.size, xmodemPacket.crc16)) {
                    // valid packet
                    file.write(xmodemPacket.buffer.bytes, xmodemPacket.buffer.size);
                    sendControl(meshtastic_XModem_Control_ACK);
                    packetno++;
                    break;
                }
                // invalid packet
                sendControl(meshtastic_XModem_Control_NAK);
                break;
            } else if (isTransmitting) {
                // just received something weird.
                sendControl(meshtastic_XModem_Control_CAN);
                isTransmitting = false;
                break;
            }
        }
        break;
    case meshtastic_XModem_Control_EOT:
        // End of transmission
        sendControl(meshtastic_XModem_Control_ACK);
        file.flush();
        file.close();
        isReceiving = false;
        break;
    case meshtastic_XModem_Control_CAN:
        // Cancel transmission and remove file
        sendControl(meshtastic_XModem_Control_ACK);
        file.flush();
        file.close();
        FSCom.remove(filename);
        isReceiving = false;
        break;
    case meshtastic_XModem_Control_ACK:
        // Acknowledge Send the next packet
        if (isTransmitting) {
            if (isEOT) {
                sendControl(meshtastic_XModem_Control_EOT);
                file.close();
                LOG_INFO("XModem: Finished sending file %s\n", filename);
                isTransmitting = false;
                isEOT = false;
                break;
            }
            retrans = MAXRETRANS; // reset retransmit counter
            packetno++;
            xmodemStore = meshtastic_XModem_init_zero;
            xmodemStore.control = meshtastic_XModem_Control_SOH;
            xmodemStore.seq = packetno;
            xmodemStore.buffer.size = file.read(xmodemStore.buffer.bytes, sizeof(meshtastic_XModem_buffer_t::bytes));
            xmodemStore.crc16 = crc16_ccitt(xmodemStore.buffer.bytes, xmodemStore.buffer.size);
            LOG_DEBUG("XModem: ACK Notify Sending packet %d, %d Bytes.\n", packetno, xmodemStore.buffer.size);
            if (xmodemStore.buffer.size < sizeof(meshtastic_XModem_buffer_t::bytes)) {
                isEOT = true;
                // send EOT on next Ack
            }
            packetReady.notifyObservers(packetno);
        } else {
            // just received something weird.
            sendControl(meshtastic_XModem_Control_CAN);
        }
        break;
    case meshtastic_XModem_Control_NAK:
        // Negative acknowledge. Send the same buffer again
        if (isTransmitting) {
            if (--retrans <= 0) {
                sendControl(meshtastic_XModem_Control_CAN);
                file.close();
                LOG_INFO("XModem: Retransmit timeout, cancelling file %s\n", filename);
                isTransmitting = false;
                break;
            }
            xmodemStore = meshtastic_XModem_init_zero;
            xmodemStore.control = meshtastic_XModem_Control_SOH;
            xmodemStore.seq = packetno;
            file.seek((packetno - 1) * sizeof(meshtastic_XModem_buffer_t::bytes));
            xmodemStore.buffer.size = file.read(xmodemStore.buffer.bytes, sizeof(meshtastic_XModem_buffer_t::bytes));
            xmodemStore.crc16 = crc16_ccitt(xmodemStore.buffer.bytes, xmodemStore.buffer.size);
            LOG_DEBUG("XModem: NAK Notify Sending packet %d, %d Bytes.\n", packetno, xmodemStore.buffer.size);
            if (xmodemStore.buffer.size < sizeof(meshtastic_XModem_buffer_t::bytes)) {
                isEOT = true;
                // send EOT on next Ack
            }
            packetReady.notifyObservers(packetno);
        } else {
            // just received something weird.
            sendControl(meshtastic_XModem_Control_CAN);
        }
        break;
    default:
        // Unknown control character
        break;
    }
}
