/**
 * @file Unishox2.cpp
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
 */

#include "Unishox2.h"
#include <string.h>
#include <assert.h>

// Include the official Unishox2 C library
extern "C" {
#include "unishox2.h"
}

namespace stechat {

size_t Unishox2::compress(const char* input, uint8_t* output, size_t outputMaxLen) {
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

size_t Unishox2::compress(const char* input, size_t inputLen,
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

    // Clamp output length to prevent integer overflow
    if (outputMaxLen > MAX_OUTPUT_LEN) {
        outputMaxLen = MAX_OUTPUT_LEN;
    }

    // Call the official Unishox2 library
    // The library returns the number of bytes written, or negative on error
    int result = unishox2_compress_simple(
        input,
        static_cast<int>(inputLen),
        reinterpret_cast<char*>(output)
    );

    // NASA Rule 7: Check return value
    if (result < 0) {
        return 0;
    }

    // Verify result is within bounds
    size_t compressedLen = static_cast<size_t>(result);
    if (compressedLen > outputMaxLen) {
        // Should not happen with proper buffer, but check anyway
        return 0;
    }

    return compressedLen;
}

size_t Unishox2::decompress(const uint8_t* input, size_t inputLen,
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
        return 0;
    }

    // Clamp output length to maximum (NASA Rule 2: fixed bounds)
    if (outputMaxLen > MAX_OUTPUT_LEN) {
        outputMaxLen = MAX_OUTPUT_LEN;
    }

    // Call the official Unishox2 library
    int result = unishox2_decompress_simple(
        reinterpret_cast<const char*>(input),
        static_cast<int>(inputLen),
        output
    );

    // NASA Rule 7: Check return value
    if (result < 0) {
        output[0] = '\0';
        return 0;
    }

    size_t decompressedLen = static_cast<size_t>(result);

    // Ensure we don't exceed buffer and null-terminate
    if (decompressedLen >= outputMaxLen) {
        decompressedLen = outputMaxLen - 1;
    }
    output[decompressedLen] = '\0';

    return decompressedLen;
}

} // namespace stechat
