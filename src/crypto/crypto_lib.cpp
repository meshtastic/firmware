// Backend over the Crypto library, including the CCM mode built on its block
// cipher.
#define TEST_CURVE25519_FIELD_OPS // makes Curve25519::isWeakPoint public
#include "crypto_backends.h"
#include "mesh/HardwareRNG.h"
#include <AES.h>
#include <CTR.h>
#include <Crypto.h>
#include <RNG.h>
#include <SHA256.h>
#include <SHA512.h>
#include <string.h>

#if !(MESHTASTIC_EXCLUDE_PKI)
#include <Curve25519.h>
#if !(MESHTASTIC_EXCLUDE_XEDDSA)
#include <Ed25519.h>
#include <XEdDSA.h>
#endif
#endif

static constexpr size_t AES_BLOCK = 16;
static constexpr size_t CCM_L = 2;

static bool ctrRun(CTRCommon &ctr, const uint8_t *key, size_t key_len, const uint8_t counter[16], const uint8_t *in, size_t len,
                   uint8_t *out)
{
    if (!ctr.setKey(key, key_len) || !ctr.setIV(counter, 16) || !ctr.setCounterSize(4))
        return false;
    ctr.encrypt(out, in, len);
    return true;
}

bool crypto_lib_aes256_ctr(const uint8_t key[32], const uint8_t counter[16], const uint8_t *in, size_t len, uint8_t *out)
{
    CTR<AES256> ctr;
    return ctrRun(ctr, key, 32, counter, in, len, out);
}

bool crypto_lib_aes_ctr(const uint8_t *key, size_t key_len, const uint8_t counter[16], const uint8_t *in, size_t len,
                        uint8_t *out)
{
    if (key_len != 16)
        return crypto_lib_aes256_ctr(key, counter, in, len, out);
    CTR<AES128> ctr;
    return ctrRun(ctr, key, 16, counter, in, len, out);
}

static void putBe16(uint8_t *a, uint16_t val)
{
    a[0] = val >> 8;
    a[1] = val & 0xff;
}

static void xorBlock(uint8_t *dst, const uint8_t *src)
{
    for (size_t i = 0; i < AES_BLOCK; i++)
        dst[i] ^= src[i];
}

static void ccmAuthStart(BlockCipher &aes, size_t tag_len, const uint8_t *nonce, const uint8_t *aad, size_t aad_len, size_t len,
                         uint8_t *x)
{
    uint8_t aad_buf[2 * AES_BLOCK];
    uint8_t b[AES_BLOCK];
    b[0] = aad_len ? 0x40 : 0;
    b[0] |= ((tag_len - 2) / 2) << 3;
    b[0] |= CCM_L - 1;
    memcpy(&b[1], nonce, 15 - CCM_L);
    putBe16(&b[AES_BLOCK - CCM_L], len);
    aes.encryptBlock(x, b);
    if (!aad_len)
        return;
    putBe16(aad_buf, aad_len);
    memcpy(aad_buf + 2, aad, aad_len);
    memset(aad_buf + 2 + aad_len, 0, sizeof(aad_buf) - 2 - aad_len);
    xorBlock(aad_buf, x);
    aes.encryptBlock(x, aad_buf);
    if (aad_len > AES_BLOCK - 2) {
        xorBlock(&aad_buf[AES_BLOCK], x);
        aes.encryptBlock(x, &aad_buf[AES_BLOCK]);
    }
}

static void ccmAuth(BlockCipher &aes, const uint8_t *data, size_t len, uint8_t *x)
{
    const size_t last = len % AES_BLOCK;
    for (size_t i = 0; i < len / AES_BLOCK; i++) {
        xorBlock(x, data);
        data += AES_BLOCK;
        aes.encryptBlock(x, x);
    }
    if (last) {
        for (size_t i = 0; i < last; i++)
            x[i] ^= *data++;
        aes.encryptBlock(x, x);
    }
}

static void ccmCounterStart(const uint8_t *nonce, uint8_t *a)
{
    a[0] = CCM_L - 1;
    memcpy(&a[1], nonce, 15 - CCM_L);
}

// The keystream goes through tmp, not out, so that in == out works.
static void ccmCrypt(BlockCipher &aes, const uint8_t *in, size_t len, uint8_t *out, uint8_t *a)
{
    uint8_t tmp[AES_BLOCK];
    uint16_t counter = 1;
    for (size_t off = 0; off < len; off += AES_BLOCK, counter++) {
        putBe16(&a[AES_BLOCK - CCM_L], counter);
        aes.encryptBlock(tmp, a);
        const size_t n = len - off < AES_BLOCK ? len - off : AES_BLOCK;
        for (size_t i = 0; i < n; i++)
            out[off + i] = tmp[i] ^ in[off + i];
    }
}

// XOR with S_0, which both encrypts and decrypts the tag.
static void ccmCryptTag(BlockCipher &aes, size_t tag_len, const uint8_t *in, uint8_t *a, uint8_t *out)
{
    uint8_t tmp[AES_BLOCK];
    putBe16(&a[AES_BLOCK - CCM_L], 0);
    aes.encryptBlock(tmp, a);
    for (size_t i = 0; i < tag_len; i++)
        out[i] = in[i] ^ tmp[i];
}

static bool constantTimeEqual(const uint8_t *a, const uint8_t *b, size_t len)
{
    volatile uint8_t d = 0;
    for (size_t i = 0; i < len; i++)
        d |= a[i] ^ b[i];
    return d == 0;
}

static void ccmSeal(BlockCipher &aes, const uint8_t nonce[13], const uint8_t *aad, size_t aad_len, const uint8_t *in, size_t len,
                    uint8_t *out, uint8_t *tag, size_t tag_len)
{
    uint8_t x[AES_BLOCK], a[AES_BLOCK];
    ccmAuthStart(aes, tag_len, nonce, aad, aad_len, len, x);
    ccmAuth(aes, in, len, x);
    ccmCounterStart(nonce, a);
    ccmCrypt(aes, in, len, out, a);
    ccmCryptTag(aes, tag_len, x, a, tag);
}

static bool ccmOpen(BlockCipher &aes, const uint8_t nonce[13], const uint8_t *aad, size_t aad_len, const uint8_t *in, size_t len,
                    const uint8_t *tag, size_t tag_len, uint8_t *out)
{
    uint8_t x[AES_BLOCK], a[AES_BLOCK], t[AES_BLOCK];
    ccmCounterStart(nonce, a);
    ccmCryptTag(aes, tag_len, tag, a, t);
    ccmCrypt(aes, in, len, out, a);
    ccmAuthStart(aes, tag_len, nonce, aad, aad_len, len, x);
    ccmAuth(aes, out, len, x);
    return constantTimeEqual(x, t, tag_len);
}

void crypto_lib_aes256_ccm_seal(const uint8_t key[32], const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                                const uint8_t *in, size_t len, uint8_t *out, uint8_t *tag, size_t tag_len)
{
    AES256 aes;
    aes.setKey(key, 32);
    ccmSeal(aes, nonce, aad, aad_len, in, len, out, tag, tag_len);
}

bool crypto_lib_aes256_ccm_open(const uint8_t key[32], const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                                const uint8_t *in, size_t len, const uint8_t *tag, size_t tag_len, uint8_t *out)
{
    AES256 aes;
    aes.setKey(key, 32);
    return ccmOpen(aes, nonce, aad, aad_len, in, len, tag, tag_len, out);
}

void crypto_lib_aes_ccm_seal(const uint8_t *key, size_t key_len, const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                             const uint8_t *in, size_t len, uint8_t *out, uint8_t *tag, size_t tag_len)
{
    if (key_len != 16) {
        crypto_lib_aes256_ccm_seal(key, nonce, aad, aad_len, in, len, out, tag, tag_len);
        return;
    }
    AES128 aes;
    aes.setKey(key, 16);
    ccmSeal(aes, nonce, aad, aad_len, in, len, out, tag, tag_len);
}

bool crypto_lib_aes_ccm_open(const uint8_t *key, size_t key_len, const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                             const uint8_t *in, size_t len, const uint8_t *tag, size_t tag_len, uint8_t *out)
{
    if (key_len != 16)
        return crypto_lib_aes256_ccm_open(key, nonce, aad, aad_len, in, len, tag, tag_len, out);
    AES128 aes;
    aes.setKey(key, 16);
    return ccmOpen(aes, nonce, aad, aad_len, in, len, tag, tag_len, out);
}

template <typename H> static void updateParts(H &h, const crypto_part *parts, size_t num_parts)
{
    for (size_t i = 0; i < num_parts; i++) {
        if (parts[i].len)
            h.update(parts[i].ptr, parts[i].len);
    }
}

void crypto_lib_sha256(const crypto_part *parts, size_t num_parts, uint8_t out[32])
{
    SHA256 h;
    updateParts(h, parts, num_parts);
    h.finalize(out, 32);
}

void crypto_lib_sha512(const crypto_part *parts, size_t num_parts, uint8_t out[64])
{
    SHA512 h;
    updateParts(h, parts, num_parts);
    h.finalize(out, 64);
}

void crypto_lib_hmac_sha256(const uint8_t *key, size_t key_len, const crypto_part *parts, size_t num_parts, uint8_t out[32])
{
    SHA256 h;
    h.resetHMAC(key, key_len);
    updateParts(h, parts, num_parts);
    h.finalizeHMAC(key, key_len, out, 32);
}

bool crypto_lib_random(uint8_t *out, size_t len)
{
    if (len && !HardwareRNG::fill(out, len))
        CryptRNG.rand(out, len);
    return true;
}

void crypto_lib_random_add(const uint8_t *data, size_t len)
{
    CryptRNG.stir(data, len);
}

#if !(MESHTASTIC_EXCLUDE_PKI)
bool crypto_lib_x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t *point)
{
    return Curve25519::eval(out, scalar, point);
}

bool crypto_lib_x25519_is_weak(const uint8_t point[32])
{
    return Curve25519::isWeakPoint(point) != 0;
}

#if !(MESHTASTIC_EXCLUDE_XEDDSA)
bool crypto_lib_ed25519_verify(const uint8_t signature[64], const uint8_t pub[32], const void *msg, size_t len)
{
    return Ed25519::verify(signature, pub, msg, len);
}

// Ed25519 keeps its point arithmetic protected; only the static members here are ever used.
struct Ed25519Internals : Ed25519 {
    static void baseMul(uint8_t point[32], const uint8_t scalar[32])
    {
        limb_t a[32 / sizeof(limb_t)];
        Point p;
        BigNumberUtil::unpackLE(a, sizeof(a) / sizeof(limb_t), scalar, 32);
        mul(p, a);
        encodePoint(point, p);
        clean(a);
        clean(p);
    }
};

void crypto_lib_ed25519_basemul(uint8_t point[32], const uint8_t scalar[32])
{
    Ed25519Internals::baseMul(point, scalar);
}

void crypto_lib_xeddsa_sign(uint8_t signature[64], const uint8_t ed_priv[32], const uint8_t ed_pub[32], const void *msg,
                            size_t len, crypto_basemul_fn basemul, void *basemul_ctx)
{
    XEdDSA::sign(signature, ed_priv, ed_pub, msg, len, basemul, basemul_ctx);
}

void crypto_lib_xeddsa_keys_from_x25519(uint8_t ed_priv[32], uint8_t ed_pub[32], const uint8_t x25519_priv[32])
{
    uint8_t copy[32];
    memcpy(copy, x25519_priv, sizeof(copy));
    XEdDSA::priv_curve_to_ed_keys(copy, ed_priv, ed_pub);
    clean(copy);
}

// RFC 7748 section 4.1: y = (u - 1) / (u + 1). XEdDSA normalizes the sign bit to zero.
void crypto_lib_xeddsa_pub_from_x25519(uint8_t ed_pub[32], const uint8_t x25519_pub[32])
{
    fe u, y, one, u_minus_one, u_plus_one, u_plus_one_inv;
    fe_frombytes(u, x25519_pub);
    fe_1(one);
    fe_sub(u_minus_one, u, one);
    fe_add(u_plus_one, u, one);
    fe_invert(u_plus_one_inv, u_plus_one);
    fe_mul(y, u_minus_one, u_plus_one_inv);
    fe_tobytes(ed_pub, y);
    ed_pub[31] &= 0x7f;
}
#endif
#endif
