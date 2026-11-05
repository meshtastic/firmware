#include "crypto_backends.h"

#ifdef ARCH_ESP32
#include "DebugConfiguration.h"
#include "mbedtls/aes.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/sha256.h"
#include "mbedtls/sha512.h"
#include <string.h>

static void wipe(void *p, size_t len)
{
    mbedtls_platform_zeroize(p, len);
}

bool crypto_esp32_aes_ctr(const uint8_t *key, size_t key_len, const uint8_t counter[16], const uint8_t *in, size_t len,
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
    wipe(iv, sizeof(iv));
    wipe(stream_block, sizeof(stream_block));
    return ok;
}

// The prebuilt mbedTLS has no CCM, so it is built from hardware CBC (the MAC) and CTR (the payload).
static constexpr size_t CCM_L = 2;

static bool ccmCtrBlock(mbedtls_aes_context *ctx, const uint8_t nonce[13], uint16_t counter, const uint8_t *in, size_t len,
                        uint8_t *out)
{
    uint8_t a[16], stream_block[16];
    size_t nc_off = 0;
    a[0] = CCM_L - 1;
    memcpy(&a[1], nonce, 13);
    a[14] = counter >> 8;
    a[15] = counter & 0xff;
    const bool ok = mbedtls_aes_crypt_ctr(ctx, len, &nc_off, a, stream_block, in, out) == 0;
    wipe(a, sizeof(a));
    wipe(stream_block, sizeof(stream_block));
    return ok;
}

static bool ccmMac(mbedtls_aes_context *ctx, size_t tag_len, const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                   const uint8_t *data, size_t len, uint8_t x[16])
{
    uint8_t blocks[3 * 16] = {0}, scratch[64], headOut[3 * 16];
    size_t head = 16;
    blocks[0] = (aad_len ? 0x40 : 0) | (((tag_len - 2) / 2) << 3) | (CCM_L - 1);
    memcpy(&blocks[1], nonce, 13);
    blocks[14] = len >> 8;
    blocks[15] = len & 0xff;
    if (aad_len) {
        blocks[16] = aad_len >> 8;
        blocks[17] = aad_len & 0xff;
        memcpy(&blocks[18], aad, aad_len);
        head = aad_len + 2 > 16 ? 48 : 32;
    }
    memset(x, 0, 16);
    bool ok = mbedtls_aes_crypt_cbc(ctx, MBEDTLS_AES_ENCRYPT, head, x, blocks, headOut) == 0;
    for (size_t off = 0; ok && off + 16 <= len;) {
        const size_t n = len - off >= sizeof(scratch) ? sizeof(scratch) : (len - off) / 16 * 16;
        ok = mbedtls_aes_crypt_cbc(ctx, MBEDTLS_AES_ENCRYPT, n, x, data + off, scratch) == 0;
        off += n;
    }
    if (ok && len % 16) {
        memset(blocks, 0, 16);
        memcpy(blocks, data + len / 16 * 16, len % 16);
        ok = mbedtls_aes_crypt_cbc(ctx, MBEDTLS_AES_ENCRYPT, 16, x, blocks, scratch) == 0;
    }
    wipe(blocks, sizeof(blocks));
    wipe(scratch, sizeof(scratch));
    wipe(headOut, sizeof(headOut));
    return ok;
}

bool crypto_esp32_aes_ccm_seal(const uint8_t *key, size_t key_len, const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                               const uint8_t *in, size_t len, uint8_t *out, uint8_t *tag, size_t tag_len)
{
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    uint8_t x[16], s0[16] = {0};
    bool ok = mbedtls_aes_setkey_enc(&ctx, key, key_len * 8) == 0 && ccmMac(&ctx, tag_len, nonce, aad, aad_len, in, len, x) &&
              ccmCtrBlock(&ctx, nonce, 0, s0, 16, s0);
    ok = ok && (len == 0 || ccmCtrBlock(&ctx, nonce, 1, in, len, out));
    if (ok) {
        for (size_t i = 0; i < tag_len; i++)
            tag[i] = x[i] ^ s0[i];
    }
    mbedtls_aes_free(&ctx);
    wipe(x, sizeof(x));
    wipe(s0, sizeof(s0));
    return ok;
}

bool crypto_esp32_aes_ccm_open(const uint8_t *key, size_t key_len, const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                               const uint8_t *in, size_t len, const uint8_t *tag, size_t tag_len, uint8_t *out)
{
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    uint8_t x[16], s0[16] = {0};
    bool ok = mbedtls_aes_setkey_enc(&ctx, key, key_len * 8) == 0 && ccmCtrBlock(&ctx, nonce, 0, s0, 16, s0) &&
              (len == 0 || ccmCtrBlock(&ctx, nonce, 1, in, len, out)) && ccmMac(&ctx, tag_len, nonce, aad, aad_len, out, len, x);
    if (ok) {
        uint8_t diff = 0;
        for (size_t i = 0; i < tag_len; i++)
            diff |= x[i] ^ s0[i] ^ tag[i];
        ok = diff == 0;
    }
    if (!ok)
        wipe(out, len);
    mbedtls_aes_free(&ctx);
    wipe(x, sizeof(x));
    wipe(s0, sizeof(s0));
    return ok;
}

static bool sha256Parts(const crypto_part *parts, size_t num_parts, uint8_t out[32])
{
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    bool ok = mbedtls_sha256_starts(&ctx, 0) == 0;
    for (size_t i = 0; ok && i < num_parts; i++)
        ok = parts[i].len == 0 || mbedtls_sha256_update(&ctx, (const uint8_t *)parts[i].ptr, parts[i].len) == 0;
    ok = ok && mbedtls_sha256_finish(&ctx, out) == 0;
    mbedtls_sha256_free(&ctx);
    return ok;
}

void crypto_esp32_sha256(const crypto_part *parts, size_t num_parts, uint8_t out[32])
{
    if (!sha256Parts(parts, num_parts, out)) {
        LOG_ERROR("ESP-SHA SHA-256 failed");
        memset(out, 0, 32);
    }
}

void crypto_esp32_sha512(const crypto_part *parts, size_t num_parts, uint8_t out[64])
{
    mbedtls_sha512_context ctx;
    mbedtls_sha512_init(&ctx);
    bool ok = mbedtls_sha512_starts(&ctx, 0) == 0;
    for (size_t i = 0; ok && i < num_parts; i++)
        ok = parts[i].len == 0 || mbedtls_sha512_update(&ctx, (const uint8_t *)parts[i].ptr, parts[i].len) == 0;
    ok = ok && mbedtls_sha512_finish(&ctx, out) == 0;
    mbedtls_sha512_free(&ctx);
    if (!ok) {
        LOG_ERROR("ESP-SHA SHA-512 failed");
        memset(out, 0, 64);
    }
}

void crypto_esp32_hmac_sha256(const uint8_t *key, size_t key_len, const crypto_part *parts, size_t num_parts, uint8_t out[32])
{
    constexpr size_t BLOCK = 64;
    uint8_t k[BLOCK] = {0}, pad[BLOCK], inner[32];
    bool ok = true;
    if (key_len > BLOCK) {
        const crypto_part kp = {key, key_len};
        ok = sha256Parts(&kp, 1, k);
    } else if (key_len) {
        memcpy(k, key, key_len);
    }
    if (ok) {
        for (size_t i = 0; i < BLOCK; i++)
            pad[i] = k[i] ^ 0x36;
        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        ok = mbedtls_sha256_starts(&ctx, 0) == 0 && mbedtls_sha256_update(&ctx, pad, BLOCK) == 0;
        for (size_t i = 0; ok && i < num_parts; i++)
            ok = parts[i].len == 0 || mbedtls_sha256_update(&ctx, (const uint8_t *)parts[i].ptr, parts[i].len) == 0;
        ok = ok && mbedtls_sha256_finish(&ctx, inner) == 0;
        mbedtls_sha256_free(&ctx);
    }
    if (ok) {
        for (size_t i = 0; i < BLOCK; i++)
            pad[i] = k[i] ^ 0x5c;
        const crypto_part outer[2] = {{pad, BLOCK}, {inner, sizeof(inner)}};
        ok = sha256Parts(outer, 2, out);
    }
    wipe(k, sizeof(k));
    wipe(pad, sizeof(pad));
    wipe(inner, sizeof(inner));
    if (!ok) {
        LOG_ERROR("ESP-SHA HMAC-SHA-256 failed");
        memset(out, 0, 32);
    }
}
#endif
