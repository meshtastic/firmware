#include "HardwareRNG.h"

#include <algorithm>
#include <cstring>
#include <random>

#include "configuration.h"

#if HAS_RADIO
#include "RadioLibInterface.h"
#endif

#if defined(ARCH_NRF52)
#include <Adafruit_nRFCrypto.h>
extern Adafruit_nRFCrypto nRFCrypto;
#elif defined(ARCH_ESP32)
#include <esp_system.h>
#elif defined(ARCH_RP2040)
#include <Arduino.h>
#elif defined(ARCH_PORTDUINO)
#include <random>
#include <sys/random.h>
#include <unistd.h>
#endif

namespace HardwareRNG
{

namespace
{
void fillWithRandomDevice(uint8_t *buffer, size_t length)
{
    std::random_device rd;
    size_t offset = 0;
    while (offset < length) {
        uint32_t value = rd();
        size_t toCopy = std::min(length - offset, sizeof(value));
        memcpy(buffer + offset, &value, toCopy);
        offset += toCopy;
    }
}

#if HAS_RADIO
bool mixWithLoRaEntropy(uint8_t *buffer, size_t length)
{
    // Only attempt to pull entropy from the modem if it is initialized and exposes the helper.
    // When the radio stack is disabled or has not yet been configured, we simply skip this step
    // and return false so callers know no extra mixing occurred.
    RadioLibInterface *radio = RadioLibInterface::instance;
    if (!radio) {
        return false;
    }

    constexpr size_t chunkSize = 16;
    uint8_t scratch[chunkSize];
    size_t offset = 0;
    bool mixed = false;

    while (offset < length) {
        size_t toCopy = std::min(length - offset, chunkSize);

        // randomBytes() returns false if the modem does not support it or is not ready
        // (for instance, when the radio is powered down). We break immediately to avoid
        // blocking or returning partially-filled entropy and simply report failure.
        if (!radio->randomBytes(scratch, toCopy)) {
            break;
        }

        for (size_t i = 0; i < toCopy; ++i) {
            buffer[offset + i] ^= scratch[i];
        }

        mixed = true;
        offset += toCopy;
    }

    // Avoid leaving the modem-sourced bytes sitting on the stack longer than needed.
    if (mixed) {
        memset(scratch, 0, sizeof(scratch));
    }

    return mixed;
}
#endif
} // namespace

bool fill(uint8_t *buffer, size_t length)
{
    if (!buffer || length == 0) {
        return false;
    }

    bool filled = false;

#if defined(ARCH_NRF52)
    // The Nordic SDK RNG provides cryptographic-quality randomness backed by hardware.
    nRFCrypto.begin();
    auto result = nRFCrypto.Random.generate(buffer, length);
    nRFCrypto.end();
    filled = result;
#elif defined(ARCH_ESP32)
    // ESP32 exposes a true RNG via esp_fill_random().
    esp_fill_random(buffer, length);
    filled = true;
#elif defined(ARCH_RP2040)
    // RP2040 has a hardware random number generator accessible through the Arduino core.
    size_t offset = 0;
    while (offset < length) {
        uint32_t value = rp2040.hwrand32();
        size_t toCopy = std::min(length - offset, sizeof(value));
        memcpy(buffer + offset, &value, toCopy);
        offset += toCopy;
    }
    filled = true;
#elif defined(ARCH_PORTDUINO)
    // Prefer the host OS RNG first when running under Portduino.
    ssize_t generated = ::getrandom(buffer, length, 0);
    if (generated == static_cast<ssize_t>(length)) {
        filled = true;
    }

    if (!filled) {
        fillWithRandomDevice(buffer, length);
        filled = true;
    }
#else
    fillWithRandomDevice(buffer, length);
    filled = true;
#endif

    if (!filled) {
        // As a last resort, fall back to std::random_device. This should only be reached
        // if a platform-specific source was unavailable.
        fillWithRandomDevice(buffer, length);
        filled = true;
    }

#if HAS_RADIO
    // Best-effort: if the radio is active and can provide modem entropy, XOR it over the
    // buffer to improve overall quality. We ignore failures to keep RNG usable even when
    // radio hardware is powered down or uninitialized.
    filled = mixWithLoRaEntropy(buffer, length) || filled;
#endif

    return filled;
}

bool seed(uint32_t &seedOut)
{
    uint32_t candidate = 0;
    if (!fill(reinterpret_cast<uint8_t *>(&candidate), sizeof(candidate))) {
        return false;
    }
    seedOut = candidate;
    return true;
}

} // namespace HardwareRNG
