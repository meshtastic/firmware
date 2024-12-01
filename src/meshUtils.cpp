#include "meshUtils.h"
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