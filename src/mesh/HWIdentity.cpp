#include "HWIdentity.h"
#include "FSCommon.h"
#include "configuration.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

#include <Curve25519.h>
#include <SHA256.h>
#include <string.h>

#ifdef ARCH_ESP32
#include <esp_system.h>
#endif

// Forward-declare the global populated at NodeDB init time.
extern meshtastic_MyNodeInfo myNodeInfo;

namespace
{
// The salt lives OUTSIDE /prefs on LittleFS/InternalFS so it survives
// NodeDB::factoryReset() which only removes /prefs. Path is short + fixed so
// operator tools can audit it if needed.
constexpr const char *kSaltPath = "/hwid_salt.bin";
constexpr const char *kLabel = "meshtastic-id-v1";
constexpr size_t kSaltSize = 32;

bool readHwRandom(uint8_t *out, size_t len)
{
#ifdef ARCH_ESP32
    // esp_fill_random is a true HW RNG once radio is up (entropy from ADC noise
    // of the RF front-end). Before radio is up the RNG falls back to a
    // ring-oscillator — still acceptable per Espressif but weaker. Our caller
    // runs from NodeDB init which is AFTER radio is up.
    esp_fill_random(out, len);
    return true;
#else
    // Fall back to whatever Arduino random() provides. Not cryptographic on
    // all platforms; caller should treat HWIdentity as unavailable if this
    // path is hit on hardware that matters.
    for (size_t i = 0; i < len; ++i) {
        out[i] = (uint8_t)random(0, 256);
    }
    return true;
#endif
}

bool loadOrGenerateSalt(uint8_t salt[kSaltSize])
{
#ifdef FSCom
    // Try to read existing salt.
    if (FSCom.exists(kSaltPath)) {
        auto f = FSCom.open(kSaltPath, FILE_O_READ);
        if (f) {
            size_t n = f.read(salt, kSaltSize);
            f.close();
            if (n == kSaltSize) {
                return true;
            }
            LOG_WARN("HWIdentity: salt file size mismatch (%u != %u), regenerating", (unsigned)n,
                     (unsigned)kSaltSize);
        } else {
            LOG_WARN("HWIdentity: could not open existing salt file, regenerating");
        }
    }

    // First-boot path: generate new salt.
    if (!readHwRandom(salt, kSaltSize)) {
        return false;
    }
    auto f = FSCom.open(kSaltPath, FILE_O_WRITE);
    if (!f) {
        LOG_ERROR("HWIdentity: failed to open salt file for write");
        return false;
    }
    size_t written = f.write(salt, kSaltSize);
    f.close();
    if (written != kSaltSize) {
        LOG_ERROR("HWIdentity: failed to persist salt (wrote %u)", (unsigned)written);
        return false;
    }
    LOG_INFO("HWIdentity: generated and stored new salt at %s", kSaltPath);
    return true;
#else
    (void)salt;
    LOG_DEBUG("HWIdentity: no filesystem available, cannot persist salt");
    return false;
#endif
}

// Curve25519 scalar clamping per RFC 7748 §5. Callers of Curve25519::eval are
// expected to do this on the scalar before multiplying.
void clampCurve25519Scalar(uint8_t s[32])
{
    s[0] &= 248;
    s[31] &= 127;
    s[31] |= 64;
}

bool haveDeviceId()
{
    // myNodeInfo.device_id is populated during NodeDB init from the per-chip
    // immutable UID (ESP32 eFuse BLK2 / nRF52 FICR). On platforms or targets
    // where we could not read it, size stays 0 and we must fall back.
    return myNodeInfo.device_id.size > 0 && myNodeInfo.device_id.size <= sizeof(myNodeInfo.device_id.bytes);
}

} // namespace

namespace HWIdentity
{

bool isAvailable()
{
    return haveDeviceId();
}

bool deriveKey(uint8_t *pubOut, uint8_t *privOut)
{
    if (!haveDeviceId()) {
        LOG_DEBUG("HWIdentity: device_id unavailable, skipping HW-derived key");
        return false;
    }

    uint8_t salt[kSaltSize];
    if (!loadOrGenerateSalt(salt)) {
        LOG_WARN("HWIdentity: salt unavailable, skipping HW-derived key");
        return false;
    }

    // seed = SHA256(device_id || salt || label)
    SHA256 sha;
    sha.reset();
    sha.update(myNodeInfo.device_id.bytes, myNodeInfo.device_id.size);
    sha.update(salt, kSaltSize);
    sha.update(reinterpret_cast<const uint8_t *>(kLabel), strlen(kLabel));
    uint8_t seed[32];
    sha.finalize(seed, sizeof(seed));

    // Clamp the scalar before Curve25519 multiplication.
    clampCurve25519Scalar(seed);

    // Derive public key. Curve25519::eval(pubOut, privIn, nullptr) performs
    // the base-point multiplication when the third argument is null/0.
    // The weak-point check is internal to the Curve25519 library and is not
    // exposed publicly; a SHA256-derived scalar hitting one of the small
    // set of known weak points has probability ~2^-128, so we accept the
    // output as given.
    Curve25519::eval(pubOut, seed, 0);

    memcpy(privOut, seed, 32);

    LOG_INFO("HWIdentity: derived Curve25519 keypair from device_id+salt (deterministic across reset)");

    // Wipe locals. We can't wipe privOut because the caller needs it.
    memset(seed, 0, sizeof(seed));
    memset(salt, 0, sizeof(salt));
    return true;
}

} // namespace HWIdentity
