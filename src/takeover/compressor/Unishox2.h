/**
 * @file Unishox2.h
 * @brief Unishox2 compression algorithm for short strings
 *
 * Based on https://github.com/siara-cc/Unishox2
 * Optimized for keystroke capture: URLs, emails, passwords, messages
 *
 * Features:
 * - Bit-level Huffman-like encoding
 * - Frequent characters get shorter codes (2-8 bits)
 * - Pre-encoded common sequences (://, https, .com, etc.)
 * - Minimal RAM usage (~512 bytes)
 *
 * Compression ratios:
 * - URLs: ~43% reduction
 * - Messages: ~42% reduction
 * - Emails: ~37% reduction
 * - Passwords: ~35% reduction
 */

#ifndef TAKEOVER_UNISHOX2_H
#define TAKEOVER_UNISHOX2_H

#include <stdint.h>
#include <stddef.h>

namespace takeover {

/**
 * @brief Unishox2 compressor for short strings
 *
 * Uses variable-length bit encoding where frequent characters
 * get shorter codes. Optimized for ASCII text with special
 * handling for URLs, emails, and common sequences.
 */
class Unishox2 {
public:
    /// Maximum input string length
    static constexpr size_t MAX_INPUT_LEN = 256;

    /// Maximum output buffer size (worst case: slight expansion)
    static constexpr size_t MAX_OUTPUT_LEN = 320;

    /**
     * @brief Compress a string using Unishox2 algorithm
     *
     * @param input Input string (null-terminated)
     * @param output Output buffer for compressed data
     * @param outputMaxLen Maximum output buffer size
     * @return Size of compressed data, or 0 on error
     */
    size_t compress(const char* input, uint8_t* output, size_t outputMaxLen);

    /**
     * @brief Compress with length parameter
     *
     * @param input Input string
     * @param inputLen Input length
     * @param output Output buffer
     * @param outputMaxLen Maximum output size
     * @return Size of compressed data
     */
    size_t compress(const char* input, size_t inputLen, uint8_t* output, size_t outputMaxLen);

    /**
     * @brief Decompress Unishox2 data
     *
     * @param input Compressed data
     * @param inputLen Compressed data length
     * @param output Output buffer for decompressed string
     * @param outputMaxLen Maximum output buffer size
     * @return Size of decompressed data, or 0 on error
     */
    size_t decompress(const uint8_t* input, size_t inputLen, char* output, size_t outputMaxLen);

    /**
     * @brief Get estimated RAM usage
     * @return RAM usage in bytes
     */
    static constexpr size_t getRAMUsage() { return 512; }

private:
    // Character sets
    enum CharSet : uint8_t {
        SET_ALPHA = 0,
        SET_SYM = 1,
        SET_NUM = 2,
    };

    // Bit buffer for encoding
    struct BitBuffer {
        uint8_t* data;
        size_t maxLen;
        size_t bytePos;
        uint8_t bitPos;

        void init(uint8_t* buf, size_t len) {
            data = buf;
            maxLen = len;
            bytePos = 0;
            bitPos = 0;
            if (len > 0) data[0] = 0;
        }

        bool writeBits(uint32_t bits, uint8_t count);
        bool readBits(uint32_t* bits, uint8_t count);
        size_t getByteCount() const;
    };

    // Encoding helpers
    bool encodeChar(BitBuffer& buf, char ch, CharSet& currentSet);
    bool encodeSequence(BitBuffer& buf, const char* str, size_t& pos);
    int getCharSetAndPos(char ch, CharSet& set);

    // Decoding helpers
    bool decodeChar(BitBuffer& buf, char& ch, CharSet& currentSet);

    // Lookup tables (static, stored in flash)
    static const char ALPHA_CHARS[];
    static const char SYM_CHARS[];
    static const char NUM_CHARS[];

    // Vertical codes: {bits, length}
    struct VCode {
        uint8_t bits;
        uint8_t length;
    };
    static const VCode VCODES[];
    static const uint8_t VCODE_COUNT;

    // Pre-encoded sequences
    struct Sequence {
        const char* str;
        uint8_t len;
    };
    static const Sequence SEQUENCES[];
    static const uint8_t SEQUENCE_COUNT;
};

} // namespace takeover

#endif // TAKEOVER_UNISHOX2_H
