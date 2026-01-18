/**
 * @file Compressor.cpp
 * @brief C++ wrapper implementation for official Unishox2 library
 *
 * Uses the official Unishox2 library from https://github.com/siara-cc/Unishox2
 * Licensed under Apache License 2.0
 *
 * NASA JPL Power of 10 Rules Compliance:
 * - All functions contain assertions for precondition checking
 * - All loops have fixed upper bounds
 * - No dynamic memory allocation
 * - No recursion or goto statements
 * - All return values are checked
 *
 * SECURITY: Uses bounded API (UNISHOX_API_WITH_OUTPUT_LEN=1) to prevent
 * buffer overflows in the underlying library.
 */

#include "Compressor.h"
#include <string.h>
#include <assert.h>

// Include the official Unishox2 C library with bounded API enabled
extern "C" {
#include "unishox2.h"
}

namespace stechat {

// Default Unishox2 presets for general text (from unishox2.h)
// These are declared as static const to avoid repeated allocation
static const unsigned char USX_HCODES_DFLT_[] = {0x00, 0x40, 0x80, 0xC0, 0xE0};
static const unsigned char USX_HCODE_LENS_DFLT_[] = {2, 2, 2, 3, 3};

size_t Compressor::compress(const char* input, uint8_t* output, size_t outputMaxLen) {
    // NASA Rule 5: Assertions for preconditions
    assert(input != nullptr && "Input pointer must not be null");
    assert(output != nullptr && "Output pointer must not be null");

    // NASA Rule 7: Validate parameters before use
    if (input == nullptr || output == nullptr) {
        return 0;
    }

    if (outputMaxLen < MIN_OUTPUT_LEN) {
        return 0;
    }

    // Calculate input length with bounded loop (NASA Rule 2)
    size_t inputLen = 0;
    for (size_t i = 0; i < MAX_INPUT_LEN && input[i] != '\0'; ++i) {
        inputLen = i + 1;
    }

    return compress(input, inputLen, output, outputMaxLen);
}

size_t Compressor::compress(const char* input, size_t inputLen,
                          uint8_t* output, size_t outputMaxLen) {
    // NASA Rule 5: Assertions for preconditions
    assert(input != nullptr && "Input pointer must not be null");
    assert(output != nullptr && "Output pointer must not be null");
    assert(inputLen <= MAX_INPUT_LEN && "Input length exceeds maximum");

    // NASA Rule 7: Validate all parameters
    if (input == nullptr || output == nullptr) {
        return 0;
    }

    if (inputLen == 0) {
        return 0;
    }

    if (outputMaxLen < MIN_OUTPUT_LEN) {
        return 0;
    }

    // Clamp input length to maximum (NASA Rule 2: fixed bounds)
    if (inputLen > MAX_INPUT_LEN) {
        inputLen = MAX_INPUT_LEN;
    }

    // Clamp output length to safe maximum
    size_t safeOutputLen = outputMaxLen;
    if (safeOutputLen > MAX_OUTPUT_LEN) {
        safeOutputLen = MAX_OUTPUT_LEN;
    }

    // Call the official Unishox2 library with BOUNDED API
    // This version takes output length and won't write beyond it
    int result = unishox2_compress(
        input,
        static_cast<int>(inputLen),
        reinterpret_cast<char*>(output),
        static_cast<int>(safeOutputLen),
        USX_HCODES_DFLT_,
        USX_HCODE_LENS_DFLT_,
        USX_FREQ_SEQ_DFLT,
        USX_TEMPLATES
    );

    // NASA Rule 7: Check return value
    // Returns compressed length on success, or value > olen on failure
    if (result < 0 || static_cast<size_t>(result) > safeOutputLen) {
        return 0;
    }

    return static_cast<size_t>(result);
}

size_t Compressor::decompress(const uint8_t* input, size_t inputLen,
                            char* output, size_t outputMaxLen) {
    // NASA Rule 5: Assertions for preconditions
    assert(input != nullptr && "Input pointer must not be null");
    assert(output != nullptr && "Output pointer must not be null");
    assert(inputLen > 0 && "Input length must be positive");
    assert(outputMaxLen >= 2 && "Output must have room for at least 1 char + null");

    // NASA Rule 7: Validate all parameters
    if (input == nullptr || output == nullptr) {
        return 0;
    }

    if (inputLen == 0 || outputMaxLen < 2) {
        output[0] = '\0';
        return 0;
    }

    // Clamp output length to maximum (NASA Rule 2: fixed bounds)
    // Reserve 1 byte for null terminator
    size_t safeOutputLen = outputMaxLen - 1;
    if (safeOutputLen > MAX_OUTPUT_LEN - 1) {
        safeOutputLen = MAX_OUTPUT_LEN - 1;
    }

    // Call the official Unishox2 library with BOUNDED API
    int result = unishox2_decompress(
        reinterpret_cast<const char*>(input),
        static_cast<int>(inputLen),
        output,
        static_cast<int>(safeOutputLen),
        USX_HCODES_DFLT_,
        USX_HCODE_LENS_DFLT_,
        USX_FREQ_SEQ_DFLT,
        USX_TEMPLATES
    );

    // NASA Rule 7: Check return value
    if (result < 0 || static_cast<size_t>(result) > safeOutputLen) {
        output[0] = '\0';
        return 0;
    }

    size_t decompressedLen = static_cast<size_t>(result);

    // Ensure null termination (defensive)
    output[decompressedLen] = '\0';

    return decompressedLen;
}

} // namespace stechat
