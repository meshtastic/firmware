/**
 * @file xmodem.cpp
 * @brief Implementation of XMODEM protocol for Meshtastic devices.
 *
 * This file contains the implementation of the XMODEM protocol for Meshtastic devices. It is based on the XMODEM implementation
 * by Georges Menie (www.menie.org) and has been adapted for protobuf encapsulation.
 *
 * The XMODEM protocol is used for reliable transmission of binary data over a serial connection. This implementation supports
 * both sending and receiving of data.
 *
 * The XModemAdapter class provides the main functionality for the protocol, including CRC calculation, packet handling, and
 * control signal sending.
 *
 * @copyright Copyright (c) 2001-2019 Georges Menie
 * @author
 * @author
 * @date
 */
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
#include "SPILock.h"
#include <cstdio>
#include <cstring>
#include <vector>

#ifdef FSCom

static const char kMflistPrefix[] = "MFLIST ";
static const size_t kMflistMaxBytes = 65536;

/** Parse `MFLIST <path> [<depth>]` from @p cmd (NUL-terminated). Default depth 0 = files in that directory only. */
static bool parseMflist(const char *cmd, char *pathOut, size_t pathCap, uint8_t *depthOut)
{
    if (strncmp(cmd, kMflistPrefix, strlen(kMflistPrefix)) != 0)
        return false;
    strlcpy(pathOut, cmd + strlen(kMflistPrefix), pathCap);
    while (pathOut[0] == ' ')
        memmove(pathOut, pathOut + 1, strlen(pathOut) + 1);
    *depthOut = 0;
    char *lastsp = strrchr(pathOut, ' ');
    if (lastsp && lastsp > pathOut) {
        bool allDigit = true;
        for (const char *q = lastsp + 1; *q; q++) {
            if (*q < '0' || *q > '9') {
                allDigit = false;
                break;
            }
        }
        if (allDigit && *(lastsp + 1)) {
            int d = atoi(lastsp + 1);
            if (d < 0)
                d = 0;
            if (d > 255)
                d = 255;
            *depthOut = (uint8_t)d;
            *lastsp = '\0';
            while (lastsp > pathOut && lastsp[-1] == ' ')
                *--lastsp = '\0';
        }
    }
    return pathOut[0] != '\0';
}

static bool appendUtf8(std::vector<uint8_t> &out, const char *s, size_t kMax)
{
    for (; *s; s++) {
        if (out.size() >= kMax) {
            const char *tail = "\n# MFLIST_TRUNCATED\n";
            for (const char *p = tail; *p && out.size() < kMax; p++)
                out.push_back((uint8_t)*p);
            return false;
        }
        out.push_back((uint8_t)*s);
    }
    return true;
}

/** UTF-8 lines `virtual_path\\tsize_bytes\\n` for XMODEM listing download. */
static bool buildListingBlob(const FSRoute &dirRoute, uint8_t levels, std::vector<uint8_t> &out)
{
    if (!fsIsDirectory(dirRoute))
        return false;

    std::vector<meshtastic_FileInfo> files;
    spiLock->lock();
    files = getFilesForRoute(dirRoute, levels);
    spiLock->unlock();

    char line[sizeof(meshtastic_FileInfo::file_name) + 32];
    for (const auto &fi : files) {
        snprintf(line, sizeof(line), "%s\t%u\n", fi.file_name, (unsigned)fi.size_bytes);
        if (!appendUtf8(out, line, kMflistMaxBytes))
            return true;
    }
    return true;
}

/** Create every parent directory for @p route.path (file path) on the routed FS. */
static bool mkdirParentsForRoute(const FSRoute &route)
{
    char tmp[sizeof(route.path)];
    strlcpy(tmp, route.path, sizeof(tmp));
    if (tmp[0] != '/')
        return false;

    // Drop basename — only directories are created here.
    char *slash = strrchr(tmp + 1, '/');
    if (!slash)
        return true; // "/file.bin" at FS root

    *slash = '\0';
    if (strlen(tmp) <= 1)
        return true;

    // Prefix mkdir at each internal '/', then the full dirname (best-effort; already-exists is OK).
    for (char *s = tmp + 1; *s; s++) {
        if (*s != '/')
            continue;
        *s = '\0';
        FSRoute d = route;
        strlcpy(d.path, tmp, sizeof(d.path));
        *s = '/';
        fsMkdir(d);
    }
    FSRoute d = route;
    strlcpy(d.path, tmp, sizeof(d.path));
    fsMkdir(d);
    return true;
}

XModemAdapter xModem;

XModemAdapter::XModemAdapter() {}

/**
 * Calculates the CRC-16 CCITT checksum of the given buffer.
 *
 * @param buffer The buffer to calculate the checksum for.
 * @param length The length of the buffer.
 * @return The calculated checksum.
 */
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

/**
 * Calculates the checksum of the given buffer and compares it to the given
 * expected checksum. Returns 1 if the checksums match, 0 otherwise.
 *
 * @param buf The buffer to calculate the checksum of.
 * @param sz The size of the buffer.
 * @param tcrc The expected checksum.
 * @return 1 if the checksums match, 0 otherwise.
 */
int XModemAdapter::check(const pb_byte_t *buf, int sz, unsigned short tcrc)
{
    return crc16_ccitt(buf, sz) == tcrc;
}

void XModemAdapter::sendControl(meshtastic_XModem_Control c)
{
    xmodemStore = meshtastic_XModem_init_zero;
    xmodemStore.control = c;
    LOG_DEBUG("XModem: Notify Send control %d", c);
    packetReady.notifyObservers(packetno);
}

meshtastic_XModem XModemAdapter::getForPhone()
{
    return xmodemStore;
}

void XModemAdapter::resetForPhone()
{
    // Clears the last fragment handed to PhoneAPI after it was read; do not reset transfer state here.
    xmodemStore = meshtastic_XModem_init_zero;
}

void XModemAdapter::captureSessionKey(const uint8_t *bytes, size_t len)
{
    if (len > sizeof(sessionKey))
        len = sizeof(sessionKey);
    memcpy(sessionKey, bytes, len);
    sessionKeyLen_ = len;
}

bool XModemAdapter::matchesSessionKey(const meshtastic_XModem &p) const
{
    return sessionKeyLen_ > 0 && p.buffer.size == sessionKeyLen_ &&
           memcmp(p.buffer.bytes, sessionKey, sessionKeyLen_) == 0;
}

void XModemAdapter::clearListing()
{
    listingBlob_.clear();
    listingReadOffset_ = 0;
    listingActive_ = false;
}

void XModemAdapter::abandonStaleTransfer()
{
    spiLock->lock();
    if (file) {
        file.flush();
        file.close();
    }
    spiLock->unlock();
    clearListing();
    sessionKeyLen_ = 0;
    isReceiving = false;
    isTransmitting = false;
    isEOT = false;
    retrans = MAXRETRANS;
    LOG_INFO("XModem: abandon stale transfer (new OPEN)");
}

void XModemAdapter::primeTransmitPacket()
{
    xmodemStore = meshtastic_XModem_init_zero;
    xmodemStore.control = meshtastic_XModem_Control_SOH;
    xmodemStore.seq = packetno;
    if (listingActive_) {
        const size_t chunkMax = sizeof(meshtastic_XModem_buffer_t::bytes);
        size_t remain = listingBlob_.size() > listingReadOffset_ ? listingBlob_.size() - listingReadOffset_ : 0;
        size_t chunk = remain > chunkMax ? chunkMax : remain;
        memcpy(xmodemStore.buffer.bytes, listingBlob_.data() + listingReadOffset_, chunk);
        xmodemStore.buffer.size = chunk;
        listingReadOffset_ += chunk;
    } else {
        spiLock->lock();
        xmodemStore.buffer.size = file.read(xmodemStore.buffer.bytes, sizeof(meshtastic_XModem_buffer_t::bytes));
        spiLock->unlock();
    }
    xmodemStore.crc16 = crc16_ccitt(xmodemStore.buffer.bytes, xmodemStore.buffer.size);
    isEOT = (xmodemStore.buffer.size < sizeof(meshtastic_XModem_buffer_t::bytes));
    LOG_DEBUG("XModem: prime packet %d, %u bytes, EOT=%d", packetno, (unsigned)xmodemStore.buffer.size, (int)isEOT);
    packetReady.notifyObservers(packetno);
}

void XModemAdapter::handlePacket(meshtastic_XModem xmodemPacket)
{
    switch (xmodemPacket.control) {
    case meshtastic_XModem_Control_SOH:
    case meshtastic_XModem_Control_STX:
        // Host Ctrl+C leaves isReceiving/isTransmitting set; a fresh OPEN (seq 0) must reset
        // or the OPEN is mis-handled as duplicate seq-0 data and the first STX is NAK'd.
        if (xmodemPacket.seq == 0 && (isReceiving || isTransmitting)) {
            abandonStaleTransfer();
        }
        if ((xmodemPacket.seq == 0) && !isReceiving && !isTransmitting) {
            // NULL packet has the destination filename (protobuf bytes are not NUL-terminated)
            size_t n = xmodemPacket.buffer.size;
            if (n >= sizeof(filename))
                n = sizeof(filename) - 1;
            memcpy(filename, xmodemPacket.buffer.bytes, n);
            filename[n] = '\0';
            size_t openKeyLen = xmodemPacket.buffer.size;
            if (openKeyLen > sizeof(sessionKey))
                openKeyLen = sizeof(sessionKey);

            if (xmodemPacket.control == meshtastic_XModem_Control_SOH) {
                if (strncmp(filename, kMflistPrefix, strlen(kMflistPrefix)) == 0) {
                    char pathBuf[sizeof(filename)];
                    uint8_t listDepth = 0;
                    if (!parseMflist(filename, pathBuf, sizeof(pathBuf), &listDepth)) {
                        sendControl(meshtastic_XModem_Control_NAK);
                        break;
                    }
                    FSRoute listRoute = fsRoute(pathBuf);
                    clearListing();
                    if (!buildListingBlob(listRoute, listDepth, listingBlob_)) {
                        sendControl(meshtastic_XModem_Control_NAK);
                        break;
                    }
                    listingActive_ = true;
                    listingReadOffset_ = 0;
                    captureSessionKey(xmodemPacket.buffer.bytes, openKeyLen);
                    sendControl(meshtastic_XModem_Control_ACK);
                    isTransmitting = true;
                    packetno = 1;
                    retrans = MAXRETRANS;
                    isEOT = false;
                    LOG_INFO("XModem: MFLIST %s depth=%u (%u bytes)", pathBuf, listDepth, (unsigned)listingBlob_.size());
                    primeTransmitPacket();
                    break;
                }

                // Receive this file and put to Flash
                activeRoute_ = fsRoute(filename);
                spiLock->lock();
                if (fsExists(activeRoute_)) fsRemove(activeRoute_);
                mkdirParentsForRoute(activeRoute_);
                file = fsOpenWrite(activeRoute_);
                spiLock->unlock();
                if (file) {
                    captureSessionKey(xmodemPacket.buffer.bytes, openKeyLen);
                    sendControl(meshtastic_XModem_Control_ACK);
                    isReceiving = true;
                    packetno = 1;
                    break;
                }
                sendControl(meshtastic_XModem_Control_NAK);
                isReceiving = false;
                break;
            }

            // STX — transmit this file from flash
            activeRoute_ = fsRoute(filename);
            LOG_INFO("XModem: Transmit file %s", filename);
            spiLock->lock();
            file = fsOpenRead(activeRoute_);
            spiLock->unlock();
            if (file) {
                captureSessionKey(xmodemPacket.buffer.bytes, openKeyLen);
                packetno = 1;
                isTransmitting = true;
                retrans = MAXRETRANS;
                isEOT = false;
                primeTransmitPacket();
                break;
            }
            sendControl(meshtastic_XModem_Control_NAK);
            isTransmitting = false;
            break;
        } else {
            if (isTransmitting && xmodemPacket.seq == 0 && matchesSessionKey(xmodemPacket)) {
                sendControl(meshtastic_XModem_Control_ACK);
                break;
            }
            if (isReceiving) {
                if (xmodemPacket.seq == 0) {
                    sendControl(meshtastic_XModem_Control_ACK);
                    break;
                }
                if (xmodemPacket.seq + 1 == packetno) {
                    sendControl(meshtastic_XModem_Control_ACK);
                    break;
                }
                if ((xmodemPacket.seq == packetno) &&
                    check(xmodemPacket.buffer.bytes, xmodemPacket.buffer.size, xmodemPacket.crc16)) {
                    spiLock->lock();
                    file.write(xmodemPacket.buffer.bytes, xmodemPacket.buffer.size);
                    spiLock->unlock();
                    sendControl(meshtastic_XModem_Control_ACK);
                    packetno++;
                    break;
                }
                sendControl(meshtastic_XModem_Control_NAK);
                break;
            } else if (isTransmitting) {
                sendControl(meshtastic_XModem_Control_CAN);
                isTransmitting = false;
                clearListing();
                sessionKeyLen_ = 0;
                if (file)
                    file.close();
                break;
            }
        }
        break;
    case meshtastic_XModem_Control_EOT:
        sendControl(meshtastic_XModem_Control_ACK);
        spiLock->lock();
        if (file) {
            file.flush();
            file.close();
        }
        spiLock->unlock();
        isReceiving = false;
        break;
    case meshtastic_XModem_Control_CAN:
        sendControl(meshtastic_XModem_Control_ACK);
        spiLock->lock();
        if (file) {
            file.flush();
            file.close();
            fsRemove(activeRoute_);
        }
        spiLock->unlock();
        isReceiving = false;
        break;
    case meshtastic_XModem_Control_ACK:
        if (isTransmitting) {
            if (isEOT) {
                sendControl(meshtastic_XModem_Control_EOT);
                spiLock->lock();
                if (!listingActive_ && file)
                    file.close();
                spiLock->unlock();
                if (listingActive_) {
                    LOG_INFO("XModem: Finished MFLIST (%u bytes)", (unsigned)listingBlob_.size());
                    clearListing();
                } else {
                    LOG_INFO("XModem: Finished send file %s", filename);
                }
                isTransmitting = false;
                isEOT = false;
                sessionKeyLen_ = 0;
                break;
            }
            retrans = MAXRETRANS;
            packetno++;
            primeTransmitPacket();
        } else {
            sendControl(meshtastic_XModem_Control_CAN);
        }
        break;
    case meshtastic_XModem_Control_NAK:
        if (isTransmitting) {
            if (--retrans <= 0) {
                sendControl(meshtastic_XModem_Control_CAN);
                spiLock->lock();
                if (!listingActive_ && file)
                    file.close();
                spiLock->unlock();
                clearListing();
                sessionKeyLen_ = 0;
                LOG_INFO("XModem: Retransmit timeout, cancel %s", filename);
                isTransmitting = false;
                break;
            }
            if (listingActive_)
                listingReadOffset_ = (packetno - 1) * sizeof(meshtastic_XModem_buffer_t::bytes);
            else {
                spiLock->lock();
                file.seek((packetno - 1) * sizeof(meshtastic_XModem_buffer_t::bytes));
                spiLock->unlock();
            }
            primeTransmitPacket();
        } else {
            sendControl(meshtastic_XModem_Control_CAN);
        }
        break;
    default:
        break;
    }
}
#endif
