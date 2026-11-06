#pragma once
#include "configuration.h"
#include "crypto_api.h"

// ARCH_NRF54L also defines ARCH_NRF52 but has no CC310.
#if defined(ARCH_NRF52) && !defined(ARCH_NRF54L)
#define CRYPTO_HAS_CC310 1
#endif

#ifdef ARCH_ESP32
#include "soc/soc_caps.h"
// C3 and C6 have no SHA-512 engine.
#if SOC_SHA_SUPPORT_SHA512
#define CRYPTO_ESP32_HAS_SHA512 1
#endif
#endif
#ifdef ARCH_STM32WL
#define CRYPTO_HAS_STM32_AES 1
#endif

typedef bool (*crypto_basemul_fn)(void *ctx, uint8_t point[32], const uint8_t scalar[32]);

// C linkage so that a test build can --wrap them to see which backend really ran.
extern "C" {
// 128 and 256 are apart so that a target with AES-128 hardware can drop software AES-128 at link time.
bool crypto_lib_aes256_ctr(const uint8_t key[32], const uint8_t counter[16], const uint8_t *in, size_t len, uint8_t *out);
void crypto_lib_aes256_ccm_seal(const uint8_t key[32], const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                                const uint8_t *in, size_t len, uint8_t *out, uint8_t *tag, size_t tag_len);
bool crypto_lib_aes256_ccm_open(const uint8_t key[32], const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                                const uint8_t *in, size_t len, const uint8_t *tag, size_t tag_len, uint8_t *out);
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
                            size_t len, crypto_basemul_fn basemul, void *basemul_ctx);
void crypto_lib_xeddsa_keys_from_x25519(uint8_t ed_priv[32], uint8_t ed_pub[32], const uint8_t x25519_priv[32]);
void crypto_lib_xeddsa_pub_from_x25519(uint8_t ed_pub[32], const uint8_t x25519_pub[32]);
bool crypto_lib_random(uint8_t *out, size_t len);
void crypto_lib_random_add(const uint8_t *data, size_t len);

#ifdef CRYPTO_HAS_STM32_AES
bool crypto_stm32wl_aes_ctr(const uint8_t *key, size_t key_len, const uint8_t counter[16], const uint8_t *in, size_t len,
                            uint8_t *out);
bool crypto_stm32wl_aes_ccm_seal(const uint8_t *key, size_t key_len, const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                                 const uint8_t *in, size_t len, uint8_t *out, uint8_t *tag, size_t tag_len);
bool crypto_stm32wl_aes_ccm_open(const uint8_t *key, size_t key_len, const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                                 const uint8_t *in, size_t len, const uint8_t *tag, size_t tag_len, uint8_t *out);
#endif

#ifdef CRYPTO_HAS_CC310
bool crypto_nrf52_aes128_ctr(const uint8_t key[16], const uint8_t counter[16], const uint8_t *in, size_t len, uint8_t *out);
bool crypto_nrf52_aes128_ccm_seal(const uint8_t key[16], const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                                  const uint8_t *in, size_t len, uint8_t *out, uint8_t *tag, size_t tag_len);
bool crypto_nrf52_aes128_ccm_open(const uint8_t key[16], const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                                  const uint8_t *in, size_t len, const uint8_t *tag, size_t tag_len, uint8_t *out);
void crypto_nrf52_sha256(const crypto_part *parts, size_t num_parts, uint8_t out[32]);
void crypto_nrf52_hmac_sha256(const uint8_t *key, size_t key_len, const crypto_part *parts, size_t num_parts, uint8_t out[32]);
bool crypto_nrf52_x25519(uint8_t out[32], const uint8_t clamped_scalar[32], const uint8_t *point);
bool crypto_nrf52_ed25519_verify(const uint8_t signature[64], const uint8_t pub[32], const void *msg, size_t len);
bool crypto_nrf52_ed25519_basemul(uint8_t point[32], const uint8_t scalar[32]);
#endif
}

#ifdef ARCH_ESP32
bool crypto_esp32_aes_ctr(const uint8_t *key, size_t key_len, const uint8_t counter[16], const uint8_t *in, size_t len,
                          uint8_t *out);
bool crypto_esp32_aes_ccm_seal(const uint8_t *key, size_t key_len, const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                               const uint8_t *in, size_t len, uint8_t *out, uint8_t *tag, size_t tag_len);
bool crypto_esp32_aes_ccm_open(const uint8_t *key, size_t key_len, const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                               const uint8_t *in, size_t len, const uint8_t *tag, size_t tag_len, uint8_t *out);
void crypto_esp32_sha256(const crypto_part *parts, size_t num_parts, uint8_t out[32]);
void crypto_esp32_sha512(const crypto_part *parts, size_t num_parts, uint8_t out[64]);
void crypto_esp32_hmac_sha256(const uint8_t *key, size_t key_len, const crypto_part *parts, size_t num_parts, uint8_t out[32]);
#endif
