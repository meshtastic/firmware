#include "crypto_backends.h"
#include <string.h>

static bool validAesKeyLen(size_t key_len)
{
    return key_len == 16 || key_len == 32;
}

bool crypto_aes_ctr(const uint8_t *key, size_t key_len, const uint8_t counter[16], const uint8_t *in, size_t len, uint8_t *out)
{
    if (!validAesKeyLen(key_len))
        return false;
    if (len == 0)
        return true;
#if defined(CRYPTO_HAS_CC310)
    return key_len == 16 ? crypto_nrf52_aes128_ctr(key, counter, in, len, out)
                         : crypto_lib_aes256_ctr(key, counter, in, len, out);
#elif defined(ARCH_ESP32)
    return crypto_esp32_aes_ctr(key, key_len, counter, in, len, out);
#else
    return crypto_lib_aes_ctr(key, key_len, counter, in, len, out);
#endif
}

static bool validCcmParams(size_t key_len, size_t aad_len, size_t len, size_t tag_len)
{
    return validAesKeyLen(key_len) && aad_len <= 30 && len <= 0xFFFF && tag_len >= 4 && tag_len <= 16 && tag_len % 2 == 0;
}

bool crypto_aes_ccm_seal(const uint8_t *key, size_t key_len, const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                         const uint8_t *in, size_t len, uint8_t *out, uint8_t *tag, size_t tag_len)
{
    if (!validCcmParams(key_len, aad_len, len, tag_len))
        return false;
#if defined(CRYPTO_HAS_CC310)
    if (key_len == 16)
        return crypto_nrf52_aes128_ccm_seal(key, nonce, aad, aad_len, in, len, out, tag, tag_len);
    crypto_lib_aes256_ccm_seal(key, nonce, aad, aad_len, in, len, out, tag, tag_len);
    return true;
#elif defined(ARCH_ESP32)
    return crypto_esp32_aes_ccm_seal(key, key_len, nonce, aad, aad_len, in, len, out, tag, tag_len);
#else
    crypto_lib_aes_ccm_seal(key, key_len, nonce, aad, aad_len, in, len, out, tag, tag_len);
    return true;
#endif }

    bool crypto_aes_ccm_open(const uint8_t *key, size_t key_len, const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                             const uint8_t *in, size_t len, const uint8_t *tag, size_t tag_len, uint8_t *out)
    {
        if (!validCcmParams(key_len, aad_len, len, tag_len))
            return false;
#if defined(CRYPTO_HAS_CC310)
        return key_len == 16 ? crypto_nrf52_aes128_ccm_open(key, nonce, aad, aad_len, in, len, tag, tag_len, out)
                             : crypto_lib_aes256_ccm_open(key, nonce, aad, aad_len, in, len, tag, tag_len, out);
#elif defined(ARCH_ESP32)
        return crypto_esp32_aes_ccm_open(key, key_len, nonce, aad, aad_len, in, len, tag, tag_len, out);
#else
        return crypto_lib_aes_ccm_open(key, key_len, nonce, aad, aad_len, in, len, tag, tag_len, out);
#endif }

        void crypto_sha256(const crypto_part *parts, size_t num_parts, uint8_t out[32])
        {
#if defined(CRYPTO_HAS_CC310)
            crypto_nrf52_sha256(parts, num_parts, out);
#elif defined(ARCH_ESP32)
            crypto_esp32_sha256(parts, num_parts, out);
#else
            crypto_lib_sha256(parts, num_parts, out);
#endif }

            void crypto_sha512(const crypto_part *parts, size_t num_parts, uint8_t out[64])
            {
#ifdef CRYPTO_ESP32_HAS_SHA512
                crypto_esp32_sha512(parts, num_parts, out);
#else
                crypto_lib_sha512(parts, num_parts, out);
#endif
            }

            void crypto_hmac_sha256(const uint8_t *key, size_t key_len, const crypto_part *parts, size_t num_parts,
                                    uint8_t out[32])
            {
#if defined(CRYPTO_HAS_CC310)
                crypto_nrf52_hmac_sha256(key, key_len, parts, num_parts, out);
#elif defined(ARCH_ESP32)
                crypto_esp32_hmac_sha256(key, key_len, parts, num_parts, out);
#else
                crypto_lib_hmac_sha256(key, key_len, parts, num_parts, out);
#endif }

                bool crypto_random(uint8_t *out, size_t len)
                {
                    return crypto_lib_random(out, len);
                }

                void crypto_random_add(const uint8_t *data, size_t len)
                {
                    crypto_lib_random_add(data, len);
                }

#if !(MESHTASTIC_EXCLUDE_PKI)
                bool crypto_x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t *point)
                {
                    uint8_t clamped[32];
                    memcpy(clamped, scalar, sizeof(clamped));
                    clamped[0] &= 0xF8;
                    clamped[31] = (clamped[31] & 0x7F) | 0x40;
                    bool ok;
#ifdef CRYPTO_HAS_CC310
                    ok = crypto_nrf52_x25519(out, clamped, point);
#else
                    ok = crypto_lib_x25519(out, clamped, point);
#endif
                    // Callers that ignore the result then see a weak key, not a stale one.
                    if (!ok)
                        memset(out, 0, 32);
                    volatile uint8_t *wipe = clamped;
                    for (size_t i = 0; i < sizeof(clamped); i++)
                        wipe[i] = 0;
                    return ok;
                }

                bool crypto_x25519_is_weak(const uint8_t point[32])
                {
                    return crypto_lib_x25519_is_weak(point);
                }

#if !(MESHTASTIC_EXCLUDE_XEDDSA)
                // Checked here so that no backend can accept a malleated signature.
                static bool isCanonicalS(const uint8_t s[32])
                {
                    // q = 2^252 + 27742317777372353535851937790883648493, little-endian
                    static const uint8_t q[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
                                                  0xa2, 0xde, 0xf9, 0xde, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10};
                    for (int i = 31; i >= 0; i--) {
                        if (s[i] != q[i])
                            return s[i] < q[i];
                    }
                    return false;
                }

                bool crypto_ed25519_verify(const uint8_t signature[64], const uint8_t pub[32], const void *msg, size_t len)
                {
                    if (!isCanonicalS(signature + 32))
                        return false;
#ifdef CRYPTO_HAS_CC310
                    return crypto_nrf52_ed25519_verify(signature, pub, msg, len);
#else
                    return crypto_lib_ed25519_verify(signature, pub, msg, len);
#endif
                }

                bool crypto_ed25519_basemul(uint8_t point[32], const uint8_t scalar[32])
                {
#ifdef CRYPTO_HAS_CC310
                    return crypto_nrf52_ed25519_basemul(point, scalar);
#else
                    crypto_lib_ed25519_basemul(point, scalar);
                    return true;
#endif
                }

#ifdef CRYPTO_HAS_CC310
// XEdDSA::sign computes R in software after a false return, so the failure is recorded for the caller to discard that.
static bool nrf52BaseMulHook(void *failed, uint8_t point[32], const uint8_t scalar[32])
{
    const bool ok = crypto_nrf52_ed25519_basemul(point, scalar);
    if (!ok)
        *static_cast<bool *>(failed) = true;
    return ok;
}
#endif

                bool crypto_xeddsa_sign(uint8_t signature[64], const uint8_t ed_priv[32], const uint8_t ed_pub[32],
                                        const void *msg, size_t len)
                {
                    // XEdDSA mixes signature[0..31] into the nonce as its random Z.
                    crypto_random(signature, 32);
#ifdef CRYPTO_HAS_CC310
                    bool failed = false;
                    crypto_lib_xeddsa_sign(signature, ed_priv, ed_pub, msg, len, nrf52BaseMulHook, &failed);
                    if (failed) {
                        memset(signature, 0, 64);
                        return false;
                    }
                #else
                                    crypto_lib_xeddsa_sign(signature, ed_priv, ed_pub, msg, len, nullptr, nullptr);
                #endif
                    return true;
                }

                void crypto_xeddsa_keys_from_x25519(uint8_t ed_priv[32], uint8_t ed_pub[32], const uint8_t x25519_priv[32])
                {
                    crypto_lib_xeddsa_keys_from_x25519(ed_priv, ed_pub, x25519_priv);
                }

                void crypto_xeddsa_pub_from_x25519(uint8_t ed_pub[32], const uint8_t x25519_pub[32])
                {
                    crypto_lib_xeddsa_pub_from_x25519(ed_pub, x25519_pub);
                }
#endif
#endif
