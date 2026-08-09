#include "meshUtils.h"
#include "target_specific.h"
#include <string.h>

/*
 * Find the first occurrence of find in s, where the search is limited to the
 * first slen characters of s.
 * -
 * Copyright (c) 2001 Mike Barcroft <mike@FreeBSD.org>
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
char *strnstr(const char *s, const char *find, size_t slen)
{
    char c;
    if ((c = *find++) != '\0') {
        char sc;
        size_t len;

        len = strlen(find);
        do {
            do {
                if (slen-- < 1 || (sc = *s++) == '\0')
                    return (NULL);
            } while (sc != c);
            if (len > slen)
                return (NULL);
        } while (strncmp(s, find, len) != 0);
        s--;
    }
    return ((char *)s);
}

void printBytes(const char *label, const uint8_t *p, size_t numbytes)
{
    int labelSize = strlen(label);
    char *messageBuffer = new char[labelSize + (numbytes * 3) + 2];
    strncpy(messageBuffer, label, labelSize);
    for (size_t i = 0; i < numbytes; i++)
        snprintf(messageBuffer + labelSize + i * 3, 4, " %02x", p[i]);
    strcpy(messageBuffer + labelSize + numbytes * 3, "\n");
    LOG_DEBUG(messageBuffer);
    delete[] messageBuffer;
}

bool memfll(const uint8_t *mem, uint8_t find, size_t numbytes)
{
    for (uint8_t i = 0; i < numbytes; i++) {
        if (mem[i] != find)
            return false;
    }
    return true;
}

bool getMacAddrDeviceId(uint8_t *deviceId)
{
    // Zero-initialized: getMacAddr() may return without writing (e.g. Portduino with no MAC
    // source), and we want that no-write case to hit the all-zeros guard below, not read garbage.
    uint8_t mac[6] = {0};
    getMacAddr(mac);
    if (memfll(mac, 0, sizeof(mac))) {
        LOG_WARN("MAC is all zeros, leaving device_id unset");
        return false;
    }
    memcpy(deviceId, mac, sizeof(mac));
    return true;
}

bool isOneOf(int item, int count, ...)
{
    va_list args;
    va_start(args, count);
    bool found = false;
    for (int i = 0; i < count; ++i) {
        if (item == va_arg(args, int)) {
            found = true;
            break;
        }
    }
    va_end(args);
    return found;
}

const std::string vformat(const char *const zcFormat, ...)
{
    va_list vaArgs;
    va_start(vaArgs, zcFormat);
    va_list vaArgsCopy;
    va_copy(vaArgsCopy, vaArgs);
    const int iLen = std::vsnprintf(NULL, 0, zcFormat, vaArgsCopy);
    va_end(vaArgsCopy);
    std::vector<char> zc(iLen + 1);
    std::vsnprintf(zc.data(), zc.size(), zcFormat, vaArgs);
    va_end(vaArgs);
    return std::string(zc.data(), iLen);
}

size_t pb_string_length(const char *str, size_t max_len)
{
    size_t len = 0;
    for (size_t i = 0; i < max_len; i++) {
        if (str[i] != '\0') {
            len = i + 1;
        }
    }
    return len;
}

size_t utf8SequenceLength(const char *buf, size_t remaining)
{
    if (!buf || remaining == 0)
        return 0;

    const uint8_t lead = (uint8_t)buf[0];
    size_t seqLen;
    uint32_t minCodepoint; // anything below this is an overlong encoding of a shorter sequence
    if (lead <= 0x7F) {
        return 1;
    } else if ((lead & 0xE0) == 0xC0) {
        seqLen = 2;
        minCodepoint = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
        seqLen = 3;
        minCodepoint = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
        seqLen = 4;
        minCodepoint = 0x10000;
    } else {
        return 0; // continuation byte with no lead, or a 5-byte-or-longer form
    }

    if (seqLen > remaining)
        return 0;

    uint32_t codepoint = lead & (seqLen == 2 ? 0x1F : (seqLen == 3 ? 0x0F : 0x07));
    for (size_t i = 1; i < seqLen; i++) {
        if (((uint8_t)buf[i] & 0xC0) != 0x80)
            return 0;
        codepoint = (codepoint << 6) | ((uint8_t)buf[i] & 0x3F);
    }

    if (codepoint < minCodepoint || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
        return 0;

    return seqLen;
}

bool sanitizeUtf8(char *buf, size_t bufSize)
{
    if (!buf || bufSize == 0)
        return false;

    // Ensure null-terminated within buffer; report if we had to enforce it
    bool replaced = (buf[bufSize - 1] != '\0');
    buf[bufSize - 1] = '\0';

    size_t i = 0;
    size_t len = strlen(buf);

    while (i < len) {
        size_t seqLen = utf8SequenceLength(buf + i, len - i);
        if (seqLen == 0) {
            // Replace only this byte; the rest of a malformed sequence is caught on later iterations
            buf[i] = '?';
            replaced = true;
            i++;
        } else {
            i += seqLen;
        }
    }

    return replaced;
}

void clampLongName(char *longName)
{
    longName[MAX_LONG_NAME_BYTES] = '\0';
    sanitizeUtf8(longName, MAX_LONG_NAME_BYTES + 1);
}
