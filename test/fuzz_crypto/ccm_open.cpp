// libFuzzer target: crypto_aes_ccm_open on arbitrary key size, nonce, AAD, tag and ciphertext; an
// accepted message must reseal to the same tag. Built and run by bin/crypto-fuzz.sh.
#include "crypto/crypto_api.h"
#include <stdlib.h>
#include <string.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 3 + 32 + 13)
        return 0;
    size_t keyLen = (data[0] & 1) ? 16 : 32;
    size_t aadLen = data[1] % 31;
    size_t tagLen = 4 + 2 * (data[2] % 7);
    const uint8_t *key = data + 3, *nonce = data + 35;
    data += 48;
    size -= 48;
    if (size < aadLen + tagLen)
        return 0;
    const uint8_t *aad = data, *tag = data + aadLen, *ct = data + aadLen + tagLen;
    size_t len = size - aadLen - tagLen;

    uint8_t *out = (uint8_t *)malloc(len ? len : 1);
    if (crypto_aes_ccm_open(key, keyLen, nonce, aad, aadLen, ct, len, tag, tagLen, out)) {
        uint8_t again[16], *resealed = (uint8_t *)malloc(len ? len : 1);
        if (!crypto_aes_ccm_seal(key, keyLen, nonce, aad, aadLen, out, len, resealed, again, tagLen) ||
            memcmp(again, tag, tagLen) || (len && memcmp(resealed, ct, len)))
            abort();
        free(resealed);
    }
    free(out);
    return 0;
}
