/**
 * @file Unishox2.cpp
 * @brief Unishox2 compression implementation
 */

#include "Unishox2.h"
#include <string.h>
#include <ctype.h>

namespace takeover {

// Character frequency order (most frequent first)
const char Unishox2::ALPHA_CHARS[] = " etaoinsrhldcumfpgwybvkxjqz";
const char Unishox2::SYM_CHARS[] = " .,;:!?'\"-()[]{}@#$%&*+=<>/\\|~`^_";
const char Unishox2::NUM_CHARS[] = "0123456789+-*/.,:;()%";

// Vertical codes - shorter codes for frequent characters
const Unishox2::VCode Unishox2::VCODES[] = {
    {0b00, 2},          // 0: most frequent (space, 'e')
    {0b010, 3},         // 1
    {0b011, 3},         // 2
    {0b100, 3},         // 3
    {0b1010, 4},        // 4
    {0b1011, 4},        // 5
    {0b1100, 4},        // 6
    {0b11010, 5},       // 7
    {0b11011, 5},       // 8
    {0b11100, 5},       // 9
    {0b111010, 6},      // 10
    {0b111011, 6},      // 11
    {0b111100, 6},      // 12
    {0b1111010, 7},     // 13
    {0b1111011, 7},     // 14
    {0b1111100, 7},     // 15
    {0b11111010, 8},    // 16
    {0b11111011, 8},    // 17
    {0b11111100, 8},    // 18
    {0b11111101, 8},    // 19
    {0b11111110, 8},    // 20
};
const uint8_t Unishox2::VCODE_COUNT = sizeof(VCODES) / sizeof(VCODES[0]);

// Pre-encoded common sequences
const Unishox2::Sequence Unishox2::SEQUENCES[] = {
    {"://", 3},
    {"https", 5},
    {"http", 4},
    {"www.", 4},
    {".com", 4},
    {".org", 4},
    {".net", 4},
    {"the ", 4},
    {"ing ", 4},
    {"tion", 4},
    {" the", 4},
    {"@gmail", 6},
    {"@yahoo", 6},
    {"pass", 4},
    {"word", 4},
    {"user", 4},
};
const uint8_t Unishox2::SEQUENCE_COUNT = sizeof(SEQUENCES) / sizeof(SEQUENCES[0]);

// =============================================================================
// BitBuffer implementation
// =============================================================================

bool Unishox2::BitBuffer::writeBits(uint32_t bits, uint8_t count) {
    while (count > 0) {
        if (bytePos >= maxLen) return false;

        uint8_t bitsToWrite = 8 - bitPos;
        if (bitsToWrite > count) bitsToWrite = count;

        // Extract bits from MSB side
        uint8_t shift = count - bitsToWrite;
        uint8_t mask = ((1 << bitsToWrite) - 1);
        uint8_t value = (bits >> shift) & mask;

        // Write to current byte position
        data[bytePos] |= value << (8 - bitPos - bitsToWrite);

        bitPos += bitsToWrite;
        count -= bitsToWrite;

        if (bitPos >= 8) {
            bitPos = 0;
            bytePos++;
            if (bytePos < maxLen) data[bytePos] = 0;
        }
    }
    return true;
}

bool Unishox2::BitBuffer::readBits(uint32_t* bits, uint8_t count) {
    *bits = 0;
    while (count > 0) {
        if (bytePos >= maxLen) return false;

        uint8_t bitsToRead = 8 - bitPos;
        if (bitsToRead > count) bitsToRead = count;

        uint8_t shift = 8 - bitPos - bitsToRead;
        uint8_t mask = ((1 << bitsToRead) - 1);
        uint8_t value = (data[bytePos] >> shift) & mask;

        *bits = (*bits << bitsToRead) | value;

        bitPos += bitsToRead;
        count -= bitsToRead;

        if (bitPos >= 8) {
            bitPos = 0;
            bytePos++;
        }
    }
    return true;
}

size_t Unishox2::BitBuffer::getByteCount() const {
    return bitPos > 0 ? bytePos + 1 : bytePos;
}

// =============================================================================
// Character set helpers
// =============================================================================

int Unishox2::getCharSetAndPos(char ch, CharSet& set) {
    char lower = tolower(ch);

    // Check alpha set
    for (int i = 0; ALPHA_CHARS[i]; i++) {
        if (ALPHA_CHARS[i] == lower) {
            set = SET_ALPHA;
            return i;
        }
    }

    // Check numeric set
    for (int i = 0; NUM_CHARS[i]; i++) {
        if (NUM_CHARS[i] == lower) {
            set = SET_NUM;
            return i;
        }
    }

    // Check symbol set
    for (int i = 0; SYM_CHARS[i]; i++) {
        if (SYM_CHARS[i] == lower) {
            set = SET_SYM;
            return i;
        }
    }

    return -1; // Not found
}

// =============================================================================
// Encoding
// =============================================================================

bool Unishox2::encodeSequence(BitBuffer& buf, const char* str, size_t& pos) {
    // Try to match a sequence
    for (uint8_t i = 0; i < SEQUENCE_COUNT; i++) {
        const Sequence& seq = SEQUENCES[i];
        bool match = true;

        for (uint8_t j = 0; j < seq.len; j++) {
            if (tolower(str[pos + j]) != seq.str[j]) {
                match = false;
                break;
            }
        }

        if (match) {
            // Sequence marker: 111110 (6 bits)
            if (!buf.writeBits(0b111110, 6)) return false;
            // Sequence ID: 4 bits
            if (!buf.writeBits(i, 4)) return false;
            pos += seq.len;
            return true;
        }
    }
    return false;
}

bool Unishox2::encodeChar(BitBuffer& buf, char ch, CharSet& currentSet) {
    CharSet targetSet;
    int pos = getCharSetAndPos(ch, targetSet);

    if (pos < 0) {
        // Unknown character - encode as literal
        // Literal marker: 11111111 (8 bits)
        if (!buf.writeBits(0xFF, 8)) return false;
        // Raw byte
        if (!buf.writeBits((uint8_t)ch, 8)) return false;
        return true;
    }

    // Switch set if needed
    if (targetSet != currentSet) {
        // Set switch codes: 00=ALPHA, 01=SYM, 10=NUM
        if (!buf.writeBits(targetSet, 2)) return false;
        currentSet = targetSet;
    }

    // Encode position using vertical code
    if (pos < VCODE_COUNT) {
        const VCode& vc = VCODES[pos];
        if (!buf.writeBits(vc.bits, vc.length)) return false;
    } else {
        // Extended position: 111111 + 5-bit position
        if (!buf.writeBits(0b111111, 6)) return false;
        if (!buf.writeBits(pos, 5)) return false;
    }

    return true;
}

size_t Unishox2::compress(const char* input, uint8_t* output, size_t outputMaxLen) {
    if (!input || !output || outputMaxLen == 0) return 0;
    return compress(input, strlen(input), output, outputMaxLen);
}

size_t Unishox2::compress(const char* input, size_t inputLen, uint8_t* output, size_t outputMaxLen) {
    if (!input || !output || outputMaxLen == 0) return 0;
    if (inputLen == 0) return 0;
    if (inputLen > MAX_INPUT_LEN) inputLen = MAX_INPUT_LEN;

    BitBuffer buf;
    buf.init(output, outputMaxLen);

    CharSet currentSet = SET_ALPHA;
    size_t pos = 0;

    while (pos < inputLen) {
        // Try sequence match first
        if (encodeSequence(buf, input, pos)) {
            continue;
        }

        // Encode single character
        if (!encodeChar(buf, input[pos], currentSet)) {
            // Buffer full
            break;
        }
        pos++;
    }

    return buf.getByteCount();
}

// =============================================================================
// Decoding
// =============================================================================

size_t Unishox2::decompress(const uint8_t* input, size_t inputLen, char* output, size_t outputMaxLen) {
    if (!input || !output || inputLen == 0 || outputMaxLen == 0) return 0;

    BitBuffer buf;
    buf.init(const_cast<uint8_t*>(input), inputLen);

    CharSet currentSet = SET_ALPHA;
    size_t outPos = 0;

    while (buf.bytePos < inputLen && outPos < outputMaxLen - 1) {
        uint32_t bits;

        // Check for sequence marker (111110)
        size_t savedBytePos = buf.bytePos;
        uint8_t savedBitPos = buf.bitPos;

        if (buf.readBits(&bits, 6) && bits == 0b111110) {
            // Read sequence ID
            if (!buf.readBits(&bits, 4)) break;

            if (bits < SEQUENCE_COUNT) {
                const Sequence& seq = SEQUENCES[bits];
                for (uint8_t i = 0; i < seq.len && outPos < outputMaxLen - 1; i++) {
                    output[outPos++] = seq.str[i];
                }
                continue;
            }
        }

        // Restore position
        buf.bytePos = savedBytePos;
        buf.bitPos = savedBitPos;

        // Check for literal marker (11111111)
        if (buf.readBits(&bits, 8) && bits == 0xFF) {
            if (!buf.readBits(&bits, 8)) break;
            output[outPos++] = (char)bits;
            continue;
        }

        // Restore and try normal decoding
        buf.bytePos = savedBytePos;
        buf.bitPos = savedBitPos;

        // Try to read set switch (2 bits peek)
        if (!buf.readBits(&bits, 2)) break;

        if (bits <= 2) {
            // Could be set switch
            currentSet = (CharSet)bits;
        } else {
            // Restore - it's part of vertical code
            buf.bytePos = savedBytePos;
            buf.bitPos = savedBitPos;
        }

        // Decode vertical code
        // Try progressively longer codes
        savedBytePos = buf.bytePos;
        savedBitPos = buf.bitPos;

        int pos = -1;
        for (int i = 0; i < VCODE_COUNT; i++) {
            buf.bytePos = savedBytePos;
            buf.bitPos = savedBitPos;

            const VCode& vc = VCODES[i];
            if (buf.readBits(&bits, vc.length) && bits == vc.bits) {
                pos = i;
                break;
            }
        }

        if (pos >= 0) {
            // Output character based on current set
            const char* charset = (currentSet == SET_ALPHA) ? ALPHA_CHARS :
                                  (currentSet == SET_NUM) ? NUM_CHARS : SYM_CHARS;
            size_t len = strlen(charset);
            if ((size_t)pos < len) {
                output[outPos++] = charset[pos];
            }
        } else {
            // Extended position or error - skip
            buf.bytePos = savedBytePos;
            buf.bitPos = savedBitPos;
            buf.readBits(&bits, 8); // Skip byte
        }
    }

    output[outPos] = '\0';
    return outPos;
}

} // namespace takeover
