#pragma once

#include "configuration.h"

#if HAS_ETHERNET && defined(HAS_ETHERNET_OTA)

#include <stddef.h>
#include <stdint.h>

namespace OTAShared
{
constexpr size_t NONCE_SIZE = 32;
constexpr size_t HASH_SIZE = 32;
constexpr uint32_t AUTH_COOLDOWN_MS = 5000;
constexpr size_t MAX_FW_SIZE = 1024 * 1024;

const uint8_t *psk();
size_t pskSize();

void genNonce(uint8_t out[NONCE_SIZE]);
void computeAuthHash(const uint8_t *nonce, size_t nonceLen, const uint8_t *psk, size_t pskLen, uint8_t out[HASH_SIZE]);
bool constTimeEq(const uint8_t *a, const uint8_t *b, size_t n);

bool authCooldownActive();
void noteAuthFailure();
} // namespace OTAShared

#endif // HAS_ETHERNET && HAS_ETHERNET_OTA
