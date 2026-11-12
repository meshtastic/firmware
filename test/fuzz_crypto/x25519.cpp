// libFuzzer target: crypto_x25519 on arbitrary scalars and points, and Diffie-Hellman agreement
// between two arbitrary scalars. Built and run by bin/crypto-fuzz.sh.
#include "crypto/crypto_api.h"
#include <stdlib.h>
#include <string.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint8_t out[32];
    if (size >= 64)
        crypto_x25519(out, data, data + 32);
    if (size >= 32)
        crypto_x25519(out, data, nullptr);
    if (size >= 64) {
        uint8_t pubA[32], pubB[32], ab[32], ba[32];
        if (crypto_x25519(pubA, data, nullptr) && crypto_x25519(pubB, data + 32, nullptr) && crypto_x25519(ab, data, pubB) &&
            crypto_x25519(ba, data + 32, pubA) && memcmp(ab, ba, 32))
            abort();
    }
    return 0;
}
