/**
 * @file Compressor.h
 * @brief C++ wrapper for official Unishox2 compression library
 *
 * This wrapper uses the official Unishox2 library from:
 * https://github.com/siara-cc/Unishox2
 *
 * NASA JPL Power of 10 Rules Compliance:
 * - Rule 1: No goto, setjmp, or recursion
 * - Rule 2: All loops have fixed upper bounds
 * - Rule 3: No dynamic memory allocation after initialization
 * - Rule 4: Functions kept under 60 lines
 * - Rule 5: Minimum 2 assertions per function
 * - Rule 6: Data declared at smallest scope
 * - Rule 7: All return values checked
 * - Rule 8: Limited preprocessor use
 * - Rule 9: Pointer use restricted and documented
 * - Rule 10: Compiled with all warnings enabled
 */

#ifndef STECHAT_COMPRESSOR_H
#define STECHAT_COMPRESSOR_H

#include <stdint.h>
#include <stddef.h>
#include <assert.h>

// Enable bounded API for buffer safety (NASA Rule 2: fixed bounds)
#ifndef UNISHOX_API_WITH_OUTPUT_LEN
#define UNISHOX_API_WITH_OUTPUT_LEN 1
#endif

// Forward declaration - the official library is pure C
// Using bounded API versions for safety
extern "C" {
    int unishox2_compress(const char *in, int len, char *out, int olen,
                          const unsigned char usx_hcodes[],
                          const unsigned char usx_hcode_lens[],
                          const char *usx_freq_seq[],
                          const char *usx_templates[]);
    int unishox2_decompress(const char *in, int len, char *out, int olen,
                            const unsigned char usx_hcodes[],
                            const unsigned char usx_hcode_lens[],
                            const char *usx_freq_seq[],
                            const char *usx_templates[]);
}

namespace stechat {

/**
 * @brief C++ wrapper for the official Unishox2 compression library
 *
 * Provides a safe, bounded interface to the Unishox2 compression algorithm.
 * All operations are bounded and checked for safety.
 *
 * Thread Safety: This class is stateless and thread-safe for concurrent use.
 */
class Compressor {
public:
    /// Maximum input string length (NASA Rule 2: fixed bounds)
    static constexpr size_t MAX_INPUT_LEN = 256;

    /// Maximum output buffer size (worst case: slight expansion)
    static constexpr size_t MAX_OUTPUT_LEN = 320;

    /// Minimum valid output buffer size
    static constexpr size_t MIN_OUTPUT_LEN = 4;

    /**
     * @brief Compress a null-terminated string using Unishox2 algorithm
     *
     * @param input Input string (null-terminated, max MAX_INPUT_LEN bytes)
     * @param output Output buffer for compressed data (min MIN_OUTPUT_LEN bytes)
     * @param outputMaxLen Maximum output buffer size
     * @return Size of compressed data in bytes, or 0 on error
     *
     * @pre input != nullptr
     * @pre output != nullptr
     * @pre outputMaxLen >= MIN_OUTPUT_LEN
     * @post return value <= outputMaxLen on success
     */
    size_t compress(const char* input, uint8_t* output, size_t outputMaxLen);

    /**
     * @brief Compress a string with explicit length
     *
     * @param input Input string (need not be null-terminated)
     * @param inputLen Input length in bytes (max MAX_INPUT_LEN)
     * @param output Output buffer for compressed data
     * @param outputMaxLen Maximum output buffer size
     * @return Size of compressed data in bytes, or 0 on error
     *
     * @pre input != nullptr
     * @pre output != nullptr
     * @pre inputLen <= MAX_INPUT_LEN
     * @pre outputMaxLen >= MIN_OUTPUT_LEN
     */
    size_t compress(const char* input, size_t inputLen, uint8_t* output, size_t outputMaxLen);

    /**
     * @brief Decompress Unishox2 data
     *
     * @param input Compressed data
     * @param inputLen Compressed data length in bytes
     * @param output Output buffer for decompressed string
     * @param outputMaxLen Maximum output buffer size (will be null-terminated)
     * @return Size of decompressed data (excluding null terminator), or 0 on error
     *
     * @pre input != nullptr
     * @pre output != nullptr
     * @pre inputLen > 0
     * @pre outputMaxLen >= 2 (at least 1 char + null terminator)
     * @post output is null-terminated on success
     */
    size_t decompress(const uint8_t* input, size_t inputLen, char* output, size_t outputMaxLen);

    /**
     * @brief Get estimated RAM usage for this compressor
     * @return RAM usage in bytes (stateless, so minimal)
     */
    static constexpr size_t getRAMUsage() { return sizeof(Compressor); }
};

// Backward compatibility alias
using Unishox2 = Compressor;

} // namespace stechat

#endif // STECHAT_COMPRESSOR_H
