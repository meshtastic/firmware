#pragma once
#include "configuration.h"
#include "crypto_api.h"

// ARCH_NRF54L also defines ARCH_NRF52 but has no CC310.
#if defined(ARCH_NRF52) && !defined(ARCH_NRF54L)
#define CRYPTO_HAS_CC310 1
#endif

// Unavailable means the hardware gave no answer, so the caller computes it in software.
enum crypto_hw_result : uint8_t { CRYPTO_HW_OK, CRYPTO_HW_REJECTED, CRYPTO_HW_UNAVAILABLE };

typedef bool (*crypto_basemul_fn)(void *ctx, uint8_t point[32], const uint8_t scalar[32]);

bool crypto_lib_aes_ctr(const uint8_t *key, size_t key_len, const uint8_t counter[16], const uint8_t *in, size_t len,
                        uint8_t *out);
void crypto_lib_aes_ccm_seal(const uint8_t *key, size_t key_len, const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                             const uint8_t *in, size_t len, uint8_t *out, uint8_t *tag, size_t tag_len);
bool crypto_lib_aes_ccm_open(const uint8_t *key, size_t key_len, const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                             const uint8_t *in, size_t len, const uint8_t *tag, size_t tag_len, uint8_t *out);
void crypto_lib_sha256(const crypto_part *parts, size_t num_parts, uint8_t out[32]);
void crypto_lib_sha512(const crypto_part *parts, size_t num_parts, uint8_t out[64]);
void crypto_lib_hmac_sha256(const uint8_t *key, size_t key_len, const crypto_part *parts, size_t num_parts, uint8_t out[32]);
bool crypto_lib_x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t *point);
bool crypto_lib_x25519_is_weak(const uint8_t point[32]);
bool crypto_lib_ed25519_verify(const uint8_t signature[64], const uint8_t pub[32], const void *msg, size_t len);
void crypto_lib_ed25519_basemul(uint8_t point[32], const uint8_t scalar[32]);
void crypto_lib_xeddsa_sign(uint8_t signature[64], const uint8_t ed_priv[32], const uint8_t ed_pub[32], const void *msg,
                            size_t len, crypto_basemul_fn basemul);
void crypto_lib_xeddsa_keys_from_x25519(uint8_t ed_priv[32], uint8_t ed_pub[32], const uint8_t x25519_priv[32]);
void crypto_lib_xeddsa_pub_from_x25519(uint8_t ed_pub[32], const uint8_t x25519_pub[32]);
bool crypto_lib_random(uint8_t *out, size_t len);
void crypto_lib_random_add(const uint8_t *data, size_t len);

#ifdef CRYPTO_HAS_CC310
crypto_hw_result crypto_cc310_aes128_ctr(const uint8_t key[16], const uint8_t counter[16], const uint8_t *in, size_t len,
                                         uint8_t *out);
crypto_hw_result crypto_cc310_x25519(uint8_t out[32], const uint8_t clamped_scalar[32], const uint8_t *point);
crypto_hw_result crypto_cc310_ed25519_verify(const uint8_t signature[64], const uint8_t pub[32], const void *msg, size_t len);
crypto_hw_result crypto_cc310_ed25519_basemul(uint8_t point[32], const uint8_t scalar[32]);
#endif

#ifdef ARCH_ESP32
bool crypto_mbedtls_aes_ctr(const uint8_t *key, size_t key_len, const uint8_t counter[16], const uint8_t *in, size_t len,
                            uint8_t *out);
#endif
