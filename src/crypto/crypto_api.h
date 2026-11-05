#pragma once
#include <stddef.h>
#include <stdint.h>

struct crypto_part {
    const void *ptr;
    size_t len;
};

// In all of these, in and out must be the same buffer or not overlap at all. Keys are 16 or 32 bytes.

// Counter is big-endian. Only the low 32 bits are guaranteed to carry; past that it depends on the backend.
bool crypto_aes_ctr(const uint8_t *key, size_t key_len, const uint8_t counter[16], const uint8_t *in, size_t len, uint8_t *out);

// CCM with L=2. tag_len is even, 4 to 16; aad_len is at most 30; len is at most 65535.
bool crypto_aes_ccm_seal(const uint8_t *key, size_t key_len, const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                         const uint8_t *in, size_t len, uint8_t *out, uint8_t *tag, size_t tag_len);
// out is unspecified on false, so read it only after a true return.
bool crypto_aes_ccm_open(const uint8_t *key, size_t key_len, const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                         const uint8_t *in, size_t len, const uint8_t *tag, size_t tag_len, uint8_t *out);

void crypto_sha256(const crypto_part *parts, size_t num_parts, uint8_t out[32]);
void crypto_sha512(const crypto_part *parts, size_t num_parts, uint8_t out[64]);
void crypto_hmac_sha256(const uint8_t *key, size_t key_len, const crypto_part *parts, size_t num_parts, uint8_t out[32]);

// Clamps the scalar (RFC 7748). point == nullptr is the base point. False on a non-canonical point.
// No weak-point check: an all-zero out is returned as is.
bool crypto_x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t *point);

bool crypto_ed25519_verify(const uint8_t signature[64], const uint8_t pub[32], const void *msg, size_t len);
// scalar is little-endian and reduced mod q.
bool crypto_ed25519_basemul(uint8_t point[32], const uint8_t scalar[32]);

bool crypto_xeddsa_sign(uint8_t signature[64], const uint8_t ed_priv[32], const uint8_t ed_pub[32], const void *msg, size_t len);
void crypto_xeddsa_keys_from_x25519(uint8_t ed_priv[32], uint8_t ed_pub[32], const uint8_t x25519_priv[32]);
void crypto_xeddsa_pub_from_x25519(uint8_t ed_pub[32], const uint8_t x25519_pub[32]);

bool crypto_random(uint8_t *out, size_t len);
void crypto_random_add(const uint8_t *data, size_t len);
