#include "meshUtils.h"
#include "configuration.h"
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
        uint8_t b = (uint8_t)buf[i];

        // Determine expected sequence length from lead byte
        size_t seqLen;
        uint32_t minCodepoint;
        if (b <= 0x7F) {
            // ASCII - valid single byte
            i++;
            continue;
        } else if ((b & 0xE0) == 0xC0) {
            seqLen = 2;
            minCodepoint = 0x80; // Reject overlong
        } else if ((b & 0xF0) == 0xE0) {
            seqLen = 3;
            minCodepoint = 0x800;
        } else if ((b & 0xF8) == 0xF0) {
            seqLen = 4;
            minCodepoint = 0x10000;
        } else {
            // Invalid lead byte (0x80-0xBF or 0xF8+)
            buf[i] = '?';
            replaced = true;
            i++;
            continue;
        }

        // Check that we have enough bytes remaining
        if (i + seqLen > len) {
            // Truncated sequence at end of string - replace remaining bytes
            for (size_t j = i; j < len; j++) {
                buf[j] = '?';
            }
            replaced = true;
            break;
        }

        // Validate continuation bytes (must be 10xxxxxx)
        bool valid = true;
        for (size_t j = 1; j < seqLen; j++) {
            if (((uint8_t)buf[i + j] & 0xC0) != 0x80) {
                valid = false;
                break;
            }
        }

        if (valid) {
            // Decode codepoint to check for overlong encodings and surrogates
            uint32_t cp = 0;
            if (seqLen == 2)
                cp = b & 0x1F;
            else if (seqLen == 3)
                cp = b & 0x0F;
            else
                cp = b & 0x07;
            for (size_t j = 1; j < seqLen; j++)
                cp = (cp << 6) | ((uint8_t)buf[i + j] & 0x3F);

            if (cp < minCodepoint || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
                // Overlong encoding, out of Unicode range, or surrogate half
                valid = false;
            }
        }

        if (valid) {
            i += seqLen;
        } else {
            // Replace only the lead byte; continuation bytes will be caught on next iteration
            buf[i] = '?';
            replaced = true;
            i++;
        }
    }

    return replaced;
}

float parseDecimalFloat(const char *s)
{
    while (*s == ' ' || (*s >= '\t' && *s <= '\r'))
        s++;
    const bool negative = *s == '-';
    if (*s == '-' || *s == '+')
        s++;
    double value = 0, scale = 1;
    bool fraction = false, anyDigit = false;
    for (;; s++) {
        if (*s >= '0' && *s <= '9') {
            value = value * 10 + (*s - '0');
            if (fraction)
                scale *= 10;
            anyDigit = true;
        } else if (*s == '.' && !fraction) {
            fraction = true;
        } else {
            break;
        }
    }
    if (!anyDigit)
        return 0.0f;
    return static_cast<float>((negative ? -value : value) / scale);
}

void clampLongName(char *longName)
{
    longName[MAX_LONG_NAME_BYTES] = '\0';
    sanitizeUtf8(longName, MAX_LONG_NAME_BYTES + 1);
}

// Radio pins the variant wires up; reassigning one from config (e.g. as a UART) cuts off the radio.
// int64_t holds both -1 and RADIOLIB_NC (0xFFFFFFFF); the trailing -1 keeps the array non-empty.
static const int64_t radioPins[] = {
#ifdef LORA_SCK
    LORA_SCK,
#endif
#ifdef LORA_MISO
    LORA_MISO,
#endif
#ifdef LORA_MOSI
    LORA_MOSI,
#endif
#ifdef LORA_CS
    LORA_CS,
#endif
#ifdef LORA_RESET
    LORA_RESET,
#endif
#ifdef LORA_DIO0
    LORA_DIO0,
#endif
#ifdef LORA_DIO1
    LORA_DIO1,
#endif
#ifdef LORA_DIO2
    LORA_DIO2,
#endif
#ifdef LORA_DIO3
    LORA_DIO3,
#endif
#ifdef LORA_BUSY
    LORA_BUSY,
#endif
#ifdef LORA_RXEN
    LORA_RXEN,
#endif
#ifdef LORA_TXEN
    LORA_TXEN,
#endif
#ifdef SX126X_CS
    SX126X_CS,
#endif
#ifdef SX126X_BUSY
    SX126X_BUSY,
#endif
#ifdef SX126X_RESET
    SX126X_RESET,
#endif
#ifdef SX126X_DIO1
    SX126X_DIO1,
#endif
#ifdef SX126X_RXEN
    SX126X_RXEN,
#endif
#ifdef SX126X_TXEN
    SX126X_TXEN,
#endif
#ifdef SX126X_POWER_EN
    SX126X_POWER_EN,
#endif
#ifdef SX128X_CS
    SX128X_CS,
#endif
#ifdef SX128X_BUSY
    SX128X_BUSY,
#endif
#ifdef SX128X_RESET
    SX128X_RESET,
#endif
#ifdef SX128X_DIO1
    SX128X_DIO1,
#endif
#ifdef SX128X_RXEN
    SX128X_RXEN,
#endif
#ifdef SX128X_TXEN
    SX128X_TXEN,
#endif
    -1};

bool isRadioPin(uint32_t pin)
{
    for (int64_t radioPin : radioPins) {
        if (radioPin >= 0 && radioPin == (int64_t)pin)
            return true;
    }
    return false;
}
