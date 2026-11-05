#include "crypto_backends.h"

#ifdef ARCH_ESP32
#include "mbedtls/aes.h"
#include <string.h>

bool crypto_mbedtls_aes_ctr(const uint8_t *key, size_t key_len, const uint8_t counter[16], const uint8_t *in, size_t len,
                            uint8_t *out)
{
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    uint8_t iv[16], stream_block[16];
    size_t nc_off = 0;
    memcpy(iv, counter, sizeof(iv));
    const bool ok = mbedtls_aes_setkey_enc(&ctx, key, key_len * 8) == 0 &&
                    mbedtls_aes_crypt_ctr(&ctx, len, &nc_off, iv, stream_block, in, out) == 0;
    mbedtls_aes_free(&ctx);
    return ok;
}
#endif
