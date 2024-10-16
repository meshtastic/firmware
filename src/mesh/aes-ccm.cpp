/*
 * Counter with CBC-MAC (CCM) with AES
 *
 * Copyright (c) 2010-2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */
#define AES_BLOCK_SIZE 16
#include "aes-ccm.h"
#if !MESHTASTIC_EXCLUDE_PKI

static inline void WPA_PUT_BE16(uint8_t *a, uint16_t val)
{
    a[0] = val >> 8;
    a[1] = val & 0xff;
}

static void xor_aes_block(uint8_t *dst, const uint8_t *src)
{
    uint32_t *d = (uint32_t *)dst;
    uint32_t *s = (uint32_t *)src;
    *d++ ^= *s++;
    *d++ ^= *s++;
    *d++ ^= *s++;
    *d++ ^= *s++;
}
static void aes_ccm_auth_start(size_t M, size_t L, const uint8_t *nonce, const uint8_t *aad, size_t aad_len, size_t plain_len,
                               uint8_t *x)
{
    uint8_t aad_buf[2 * AES_BLOCK_SIZE];
    uint8_t b[AES_BLOCK_SIZE];
    /* Authentication */
    /* B_0: Flags | Nonce N | l(m) */
    b[0] = aad_len ? 0x40 : 0 /* Adata */;
    b[0] |= (((M - 2) / 2) /* M' */ << 3);
    b[0] |= (L - 1) /* L' */;
    memcpy(&b[1], nonce, 15 - L);
    WPA_PUT_BE16(&b[AES_BLOCK_SIZE - L], plain_len);
    crypto->aesEncrypt(b, x); /* X_1 = E(K, B_0) */
    if (!aad_len)
        return;
    WPA_PUT_BE16(aad_buf, aad_len);
    memcpy(aad_buf + 2, aad, aad_len);
    memset(aad_buf + 2 + aad_len, 0, sizeof(aad_buf) - 2 - aad_len);
    xor_aes_block(aad_buf, x);
    crypto->aesEncrypt(aad_buf, x); /* X_2 = E(K, X_1 XOR B_1) */
    if (aad_len > AES_BLOCK_SIZE - 2) {
        xor_aes_block(&aad_buf[AES_BLOCK_SIZE], x);
        /* X_3 = E(K, X_2 XOR B_2) */
        crypto->aesEncrypt(&aad_buf[AES_BLOCK_SIZE], x);
    }
}
static void aes_ccm_auth(const uint8_t *data, size_t len, uint8_t *x)
{
    size_t last = len % AES_BLOCK_SIZE;
    size_t i;
    for (i = 0; i < len / AES_BLOCK_SIZE; i++) {
        /* X_i+1 = E(K, X_i XOR B_i) */
        xor_aes_block(x, data);
        data += AES_BLOCK_SIZE;
        crypto->aesEncrypt(x, x);
    }
    if (last) {
        /* XOR zero-padded last block */
        for (i = 0; i < last; i++)
            x[i] ^= *data++;
        crypto->aesEncrypt(x, x);
    }
}
static void aes_ccm_encr_start(size_t L, const uint8_t *nonce, uint8_t *a)
{
    /* A_i = Flags | Nonce N | Counter i */
    a[0] = L - 1; /* Flags = L' */
    memcpy(&a[1], nonce, 15 - L);
}
static void aes_ccm_encr(size_t L, const uint8_t *in, size_t len, uint8_t *out, uint8_t *a)
{
    size_t last = len % AES_BLOCK_SIZE;
    size_t i;
    /* crypt = msg XOR (S_1 | S_2 | ... | S_n) */
    for (i = 1; i <= len / AES_BLOCK_SIZE; i++) {
        WPA_PUT_BE16(&a[AES_BLOCK_SIZE - 2], i);
        /* S_i = E(K, A_i) */
        crypto->aesEncrypt(a, out);
        xor_aes_block(out, in);
        out += AES_BLOCK_SIZE;
        in += AES_BLOCK_SIZE;
    }
    if (last) {
        WPA_PUT_BE16(&a[AES_BLOCK_SIZE - 2], i);
        crypto->aesEncrypt(a, out);
        /* XOR zero-padded last block */
        for (i = 0; i < last; i++)
            *out++ ^= *in++;
    }
}
static void aes_ccm_encr_auth(size_t M, const uint8_t *x, uint8_t *a, uint8_t *auth)
{
    size_t i;
    uint8_t tmp[AES_BLOCK_SIZE];
    /* U = T XOR S_0; S_0 = E(K, A_0) */
    WPA_PUT_BE16(&a[AES_BLOCK_SIZE - 2], 0);
    crypto->aesEncrypt(a, tmp);
    for (i = 0; i < M; i++)
        auth[i] = x[i] ^ tmp[i];
}
static void aes_ccm_decr_auth(size_t M, uint8_t *a, const uint8_t *auth, uint8_t *t)
{
    size_t i;
    uint8_t tmp[AES_BLOCK_SIZE];
    /* U = T XOR S_0; S_0 = E(K, A_0) */
    WPA_PUT_BE16(&a[AES_BLOCK_SIZE - 2], 0);
    crypto->aesEncrypt(a, tmp);
    for (i = 0; i < M; i++)
        t[i] = auth[i] ^ tmp[i];
}
/* AES-CCM with fixed L=2 and aad_len <= 30 assumption */
int aes_ccm_ae(const uint8_t *key, size_t key_len, const uint8_t *nonce, size_t M, const uint8_t *plain, size_t plain_len,
               const uint8_t *aad, size_t aad_len, uint8_t *crypt, uint8_t *auth)
{
    const size_t L = 2;
    uint8_t x[AES_BLOCK_SIZE], a[AES_BLOCK_SIZE];
    if (aad_len > 30 || M > AES_BLOCK_SIZE)
        return -1;
    crypto->aesSetKey(key, key_len);
    aes_ccm_auth_start(M, L, nonce, aad, aad_len, plain_len, x);
    aes_ccm_auth(plain, plain_len, x);
    /* Encryption */
    aes_ccm_encr_start(L, nonce, a);
    aes_ccm_encr(L, plain, plain_len, crypt, a);
    aes_ccm_encr_auth(M, x, a, auth);
    return 0;
}
/* AES-CCM with fixed L=2 and aad_len <= 30 assumption */
bool aes_ccm_ad(const uint8_t *key, size_t key_len, const uint8_t *nonce, size_t M, const uint8_t *crypt, size_t crypt_len,
                const uint8_t *aad, size_t aad_len, const uint8_t *auth, uint8_t *plain)
{
    const size_t L = 2;
    uint8_t x[AES_BLOCK_SIZE], a[AES_BLOCK_SIZE];
    uint8_t t[AES_BLOCK_SIZE];
    if (aad_len > 30 || M > AES_BLOCK_SIZE)
        return false;
    crypto->aesSetKey(key, key_len);
    /* Decryption */
    aes_ccm_encr_start(L, nonce, a);
    aes_ccm_decr_auth(M, a, auth, t);
    /* plaintext = msg XOR (S_1 | S_2 | ... | S_n) */
    aes_ccm_encr(L, crypt, crypt_len, plain, a);
    aes_ccm_auth_start(M, L, nonce, aad, aad_len, crypt_len, x);
    aes_ccm_auth(plain, crypt_len, x);
    if (memcmp(x, t, M) != 0) { // FIXME make const comp
        return false;
    }
    return true;
}
#endif