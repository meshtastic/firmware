#pragma once
#include "CryptoEngine.h"
#if !MESHTASTIC_EXCLUDE_PKI

int aes_ccm_ae(const uint8_t *key, size_t key_len, const uint8_t *nonce, size_t M, const uint8_t *plain, size_t plain_len,
               const uint8_t *aad, size_t aad_len, uint8_t *crypt, uint8_t *auth);

bool aes_ccm_ad(const uint8_t *key, size_t key_len, const uint8_t *nonce, size_t M, const uint8_t *crypt, size_t crypt_len,
                const uint8_t *aad, size_t aad_len, const uint8_t *auth, uint8_t *plain);
#endif