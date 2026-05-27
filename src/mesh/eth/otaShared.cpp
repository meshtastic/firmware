#include "configuration.h"

#if HAS_ETHERNET && defined(HAS_ETHERNET_OTA)

#include "otaShared.h"
#include <Arduino.h>
#include <SHA256.h>
#include <string.h>

namespace OTAShared
{

#ifdef USERPREFS_OTA_PSK
static const uint8_t kPSK[] = USERPREFS_OTA_PSK;
#else
// Default PSK (CHANGE THIS for production deployments)
// = "meshtastic_ota_default_psk_v1!!!"
static const uint8_t kPSK[] = {0x6d, 0x65, 0x73, 0x68, 0x74, 0x61, 0x73, 0x74, 0x69, 0x63, 0x5f,
                               0x6f, 0x74, 0x61, 0x5f, 0x64, 0x65, 0x66, 0x61, 0x75, 0x6c, 0x74,
                               0x5f, 0x70, 0x73, 0x6b, 0x5f, 0x76, 0x31, 0x21, 0x21, 0x21};
#endif
static constexpr size_t kPSKSize = sizeof(kPSK);

static uint32_t s_lastAuthFailure = 0;

const uint8_t *psk()
{
    return kPSK;
}

size_t pskSize()
{
    return kPSKSize;
}

void genNonce(uint8_t out[NONCE_SIZE])
{
    for (size_t i = 0; i < NONCE_SIZE; i += 4) {
        uint32_t r = random();
        size_t remaining = NONCE_SIZE - i;
        memcpy(out + i, &r, remaining < 4 ? remaining : 4);
    }
}

void computeAuthHash(const uint8_t *nonce, size_t nonceLen, const uint8_t *psk, size_t pskLen, uint8_t out[HASH_SIZE])
{
    SHA256 sha;
    sha.reset();
    sha.update(nonce, nonceLen);
    sha.update(psk, pskLen);
    sha.finalize(out, HASH_SIZE);
}

bool constTimeEq(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

bool authCooldownActive()
{
    return s_lastAuthFailure != 0 && (millis() - s_lastAuthFailure) < AUTH_COOLDOWN_MS;
}

void noteAuthFailure()
{
    s_lastAuthFailure = millis();
}

} // namespace OTAShared

#endif // HAS_ETHERNET && HAS_ETHERNET_OTA
