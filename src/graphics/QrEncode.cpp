// Stripped-down fork of ricmoo/QRCode (https://github.com/ricmoo/QRCode), itself based on
// Project Nayuki's QR Code generator (https://www.nayuki.io/page/qr-code-generator-library).
//
// The MIT License (MIT)
// Copyright (c) 2017 Richard Moore
// Copyright (c) Project Nayuki
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
// Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "QrEncode.h"

#include <string.h>

// ECC level M, versions 1-6 (index = version - 1). Every block in these versions has the same length.
static const uint8_t TOTAL_CODEWORDS[6] = {26, 44, 70, 100, 134, 172};
static const uint8_t ECC_PER_BLOCK[6] = {10, 16, 26, 18, 24, 16};
static const uint8_t NUM_BLOCKS[6] = {1, 1, 1, 2, 2, 4};

// 15-bit format words for ECC level M with masks 0-7
static const uint16_t FORMAT_BITS[8] = {0x5412, 0x5125, 0x5E7C, 0x5B4B, 0x45F9, 0x40CE, 0x4F97, 0x4AA0};

#define MAX_CODEWORDS 172
#define MAX_ECC_PER_BLOCK 26

static bool getBit(const uint8_t *m, uint8_t size, uint8_t x, uint8_t y)
{
    uint16_t i = y * size + x;
    return (m[i >> 3] >> (7 - (i & 7))) & 1;
}

static void setBit(uint8_t *m, uint8_t size, uint8_t x, uint8_t y, bool on)
{
    uint16_t i = y * size + x;
    uint8_t b = 0x80 >> (i & 7);
    if (on)
        m[i >> 3] |= b;
    else
        m[i >> 3] &= ~b;
}

// Computed rather than tracked in a second grid; versions 1-6 have no version-info area and one alignment pattern.
static bool isFunction(uint8_t size, uint8_t x, uint8_t y)
{
    if (x == 6 || y == 6)
        return true;
    if (x < 9 && (y < 9 || y >= size - 8))
        return true;
    if (y < 9 && x >= size - 8)
        return true;
    return size > 21 && (uint8_t)(x - (size - 9)) < 5 && (uint8_t)(y - (size - 9)) < 5;
}

static void drawFormatBits(uint8_t *m, uint8_t size, uint8_t mask)
{
    uint16_t bits = FORMAT_BITS[mask];
    for (uint8_t i = 0; i < 15; i++) {
        bool on = (bits >> i) & 1;
        if (i < 6)
            setBit(m, size, 8, i, on);
        else if (i < 8)
            setBit(m, size, 8, i + 1, on);
        else if (i == 8)
            setBit(m, size, 7, 8, on);
        else
            setBit(m, size, 14 - i, 8, on);

        if (i < 8)
            setBit(m, size, size - 1 - i, 8, on);
        else
            setBit(m, size, 8, size - 15 + i, on);
    }
    setBit(m, size, 8, size - 8, true); // Dark module
}

static void drawFunctionPatterns(uint8_t *m, uint8_t size)
{
    for (uint8_t i = 8; i < size - 8; i += 2) {
        setBit(m, size, 6, i, true);
        setBit(m, size, i, 6, true);
    }

    // Symmetric row bitmaps; separators are already light
    static const uint8_t FINDER[7] = {0x7F, 0x41, 0x5D, 0x5D, 0x5D, 0x41, 0x7F};
    static const uint8_t ALIGN[5] = {0x1F, 0x11, 0x15, 0x11, 0x1F};
    uint8_t edge = size - 7;
    for (uint8_t y = 0; y < 7; y++) {
        for (uint8_t x = 0; x < 7; x++) {
            if ((FINDER[y] >> x) & 1) {
                setBit(m, size, x, y, true);
                setBit(m, size, edge + x, y, true);
                setBit(m, size, x, edge + y, true);
            }
            if (size > 21 && x < 5 && y < 5 && ((ALIGN[y] >> x) & 1))
                setBit(m, size, size - 9 + x, size - 9 + y, true);
        }
    }
}

static void applyMask(uint8_t *m, uint8_t size, uint8_t mask)
{
    for (uint8_t y = 0; y < size; y++) {
        for (uint8_t x = 0; x < size; x++) {
            if (isFunction(size, x, y))
                continue;
            bool invert;
            switch (mask) {
            case 0:
                invert = (x + y) % 2 == 0;
                break;
            case 1:
                invert = y % 2 == 0;
                break;
            case 2:
                invert = x % 3 == 0;
                break;
            case 3:
                invert = (x + y) % 3 == 0;
                break;
            case 4:
                invert = (x / 3 + y / 2) % 2 == 0;
                break;
            case 5:
                invert = x * y % 2 + x * y % 3 == 0;
                break;
            case 6:
                invert = (x * y % 2 + x * y % 3) % 2 == 0;
                break;
            default:
                invert = ((x + y) % 2 + x * y % 3) % 2 == 0;
                break;
            }
            if (invert) {
                uint16_t i = y * size + x;
                m[i >> 3] ^= 0x80 >> (i & 7);
            }
        }
    }
}

static uint32_t getPenaltyScore(const uint8_t *m, uint8_t size)
{
    uint32_t result = 0;
    uint16_t dark = 0;

    for (uint8_t y = 0; y < size; y++) {
        uint8_t runRow = 0, runCol = 0;
        bool lastRow = false, lastCol = false;
        uint16_t bitsRow = 0, bitsCol = 0;

        for (uint8_t x = 0; x < size; x++) {
            bool cRow = getBit(m, size, x, y);
            bool cCol = getBit(m, size, y, x);

            if (x == 0 || cRow != lastRow) {
                lastRow = cRow;
                runRow = 1;
            } else if (++runRow == 5) {
                result += 3;
            } else if (runRow > 5) {
                result++;
            }
            if (x == 0 || cCol != lastCol) {
                lastCol = cCol;
                runCol = 1;
            } else if (++runCol == 5) {
                result += 3;
            } else if (runCol > 5) {
                result++;
            }

            if (x > 0 && y > 0 && cRow == getBit(m, size, x - 1, y - 1) && cRow == getBit(m, size, x, y - 1) &&
                cRow == getBit(m, size, x - 1, y))
                result += 3;

            // Finder-like 1:1:3:1:1 runs
            bitsRow = ((bitsRow << 1) & 0x7FF) | cRow;
            bitsCol = ((bitsCol << 1) & 0x7FF) | cCol;
            if (x >= 10) {
                if (bitsRow == 0x05D || bitsRow == 0x5D0)
                    result += 40;
                if (bitsCol == 0x05D || bitsCol == 0x5D0)
                    result += 40;
            }

            dark += cRow;
        }
    }

    uint16_t total = size * size;
    for (uint16_t k = 0; dark * 20 < (9 - k) * total || dark * 20 > (11 + k) * total; k++)
        result += 10;
    return result;
}

// Reed-Solomon over GF(2^8 / 0x11D)
static uint8_t rsMultiply(uint8_t x, uint8_t y)
{
    uint16_t z = 0;
    for (int8_t i = 7; i >= 0; i--) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return z;
}

static void rsInit(uint8_t degree, uint8_t *coeff)
{
    memset(coeff, 0, degree);
    coeff[degree - 1] = 1;
    uint8_t root = 1;
    for (uint8_t i = 0; i < degree; i++) {
        for (uint8_t j = 0; j < degree; j++) {
            coeff[j] = rsMultiply(coeff[j], root);
            if (j + 1 < degree)
                coeff[j] ^= coeff[j + 1];
        }
        root = rsMultiply(root, 0x02);
    }
}

// result must be zeroed
static void rsRemainder(uint8_t degree, const uint8_t *coeff, const uint8_t *data, uint8_t length, uint8_t *result)
{
    for (uint8_t i = 0; i < length; i++) {
        uint8_t factor = data[i] ^ result[0];
        memmove(result, result + 1, degree - 1);
        result[degree - 1] = 0;
        for (uint8_t j = 0; j < degree; j++)
            result[j] ^= rsMultiply(coeff[j], factor);
    }
}

static void appendBits(uint8_t *buf, uint16_t *offset, uint8_t val, uint8_t length)
{
    for (int8_t i = length - 1; i >= 0; i--, (*offset)++)
        buf[*offset >> 3] |= ((val >> i) & 1) << (7 - (*offset & 7));
}

bool qrEncodeText(QrEncode *qr, uint8_t *modules, const char *text)
{
    size_t length = strlen(text);
    uint8_t v = 1;
    // 4-bit mode + 8-bit count + data bytes
    while (length + 2 > (size_t)(TOTAL_CODEWORDS[v - 1] - ECC_PER_BLOCK[v - 1] * NUM_BLOCKS[v - 1])) {
        if (++v > QR_ENCODE_MAX_VERSION)
            return false;
    }

    uint8_t size = QR_ENCODE_SIZE(v);
    uint8_t total = TOTAL_CODEWORDS[v - 1];
    uint8_t eccLen = ECC_PER_BLOCK[v - 1];
    uint8_t blocks = NUM_BLOCKS[v - 1];
    uint8_t dataCap = total - eccLen * blocks;
    uint8_t blockLen = dataCap / blocks;

    qr->version = v;
    qr->size = size;
    qr->modules = modules;

    // Data blocks then ECC blocks, each contiguous; interleaving happens during placement
    uint8_t cw[MAX_CODEWORDS];
    memset(cw, 0, total);
    uint16_t bit = 0;
    appendBits(cw, &bit, 0x4, 4); // Byte mode
    appendBits(cw, &bit, length, 8);
    for (size_t i = 0; i < length; i++)
        appendBits(cw, &bit, (uint8_t)text[i], 8);
    // Terminator and byte-alignment bits are already zero
    for (uint8_t i = (bit + 7) >> 3, pad = 0xEC; i < dataCap; i++, pad ^= 0xEC ^ 0x11)
        cw[i] = pad;

    uint8_t coeff[MAX_ECC_PER_BLOCK];
    rsInit(eccLen, coeff);
    for (uint8_t b = 0; b < blocks; b++)
        rsRemainder(eccLen, coeff, cw + b * blockLen, blockLen, cw + dataCap + b * eccLen);

    memset(modules, 0, QR_ENCODE_BUFFER_SIZE(v));
    drawFunctionPatterns(modules, size);

    uint16_t i = 0;
    uint8_t cur = 0;
    for (int8_t right = size - 1; right >= 1; right -= 2) {
        if (right == 6)
            right = 5;
        for (uint8_t vert = 0; vert < size; vert++) {
            for (uint8_t j = 0; j < 2; j++) {
                uint8_t x = right - j;
                bool upwards = ((right & 2) == 0) ^ (x < 6);
                uint8_t y = upwards ? size - 1 - vert : vert;
                if (isFunction(size, x, y) || i >= total * 8)
                    continue; // Remainder bits stay light
                if ((i & 7) == 0) {
                    uint8_t k = i >> 3;
                    if (k < dataCap) {
                        cur = cw[(k % blocks) * blockLen + k / blocks];
                    } else {
                        k -= dataCap;
                        cur = cw[dataCap + (k % blocks) * eccLen + k / blocks];
                    }
                }
                if (cur & (0x80 >> (i & 7)))
                    setBit(modules, size, x, y, true);
                i++;
            }
        }
    }

    uint8_t mask = 0;
    uint32_t minPenalty = UINT32_MAX;
    for (uint8_t m = 0; m < 8; m++) {
        drawFormatBits(modules, size, m);
        applyMask(modules, size, m);
        uint32_t penalty = getPenaltyScore(modules, size);
        if (penalty < minPenalty) {
            mask = m;
            minPenalty = penalty;
        }
        applyMask(modules, size, m); // XOR again to undo
    }

    qr->mask = mask;
    drawFormatBits(modules, size, mask);
    applyMask(modules, size, mask);
    return true;
}

bool qrGetModule(const QrEncode *qr, uint8_t x, uint8_t y)
{
    if (x >= qr->size || y >= qr->size)
        return false;
    return getBit(qr->modules, qr->size, x, y);
}
