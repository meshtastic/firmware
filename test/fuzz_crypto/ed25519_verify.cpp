// libFuzzer target: crypto_ed25519_verify on arbitrary signature, key and message. Built and run
// by bin/crypto-fuzz.sh.
#include "crypto/crypto_api.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 96)
        return 0;
    crypto_ed25519_verify(data, data + 64, data + 96, size - 96);
    return 0;
}
