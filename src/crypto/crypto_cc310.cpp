#include "crypto_backends.h"

#ifdef CRYPTO_HAS_CC310
#include "concurrency/LockGuard.h"
#include <Adafruit_nRFCrypto.h>
#include <Crypto.h>
#include <nrf_cc310/include/crys_ec_mont_edw_error.h>
#include <string.h>

// nRFCrypto_AES::Process takes a uint8_t length and rounds it up to a block, so 240 is the most it can do.
static constexpr size_t CC310_CTR_MAX = 240;

// Function-local statics: at file scope their constructors would keep them out of linker GC.
static concurrency::Lock &cc310Lock()
{
    static concurrency::Lock lock;
    return lock;
}

crypto_hw_result crypto_cc310_aes128_ctr(const uint8_t key[16], const uint8_t counter[16], const uint8_t *in, size_t len,
                                         uint8_t *out)
{
    if (len > CC310_CTR_MAX)
        return CRYPTO_HW_UNAVAILABLE;
    concurrency::LockGuard guard(&cc310Lock());
    if (!nRFCrypto.begin())
        return CRYPTO_HW_UNAVAILABLE;
    // RAM copies, because the CC310 cannot DMA from flash.
    uint8_t k[16], iv[16];
    char enc[CC310_CTR_MAX];
    memcpy(k, key, sizeof(k));
    memcpy(iv, counter, sizeof(iv));
    nRFCrypto_AES ctx;
    ctx.begin();
    const int result = ctx.Process((char *)in, (uint8_t)len, iv, k, sizeof(k), enc, ctx.encryptFlag, ctx.ctrMode);
    ctx.end();
    nRFCrypto.end();
    clean(k);
    if (result < 0) {
        LOG_WARN("CC310 AES error %d, using software", result);
        return CRYPTO_HW_UNAVAILABLE;
    }
    memcpy(out, enc, len);
    return CRYPTO_HW_OK;
}

#if !(MESHTASTIC_EXCLUDE_PKI)
// True if u (top bit ignored, as RFC 7748 says) is below p = 2^255 - 19.
static bool isCanonicalPoint(const uint8_t *u)
{
    if ((u[31] & 0x7F) != 0x7F || u[0] < 0xED)
        return true;
    for (int i = 1; i < 31; i++) {
        if (u[i] != 0xFF)
            return true;
    }
    return false;
}

crypto_hw_result crypto_cc310_x25519(uint8_t out[32], const uint8_t clamped_scalar[32], const uint8_t *point)
{
    if (point && !isCanonicalPoint(point))
        return CRYPTO_HW_REJECTED;
    concurrency::LockGuard guard(&cc310Lock());
    if (!nRFCrypto.begin())
        return CRYPTO_HW_UNAVAILABLE;
    static nRFCrypto_X25519 x25519;
    const bool ok = point ? x25519.agree(out, clamped_scalar, point) : x25519.publicKey(out, clamped_scalar);
    const uint32_t err = x25519.lastError();
    nRFCrypto.end();
    if (ok)
        return CRYPTO_HW_OK;
    if (err == CRYS_OK) {
        // agree() refused an all-zero result; report it the way the software does.
        memset(out, 0, 32);
        return CRYPTO_HW_OK;
    }
    LOG_WARN("CC310 X25519 error 0x%08x, using software", (unsigned int)err);
    return CRYPTO_HW_UNAVAILABLE;
}

#if !(MESHTASTIC_EXCLUDE_XEDDSA)
static nRFCrypto_Ed25519 &cc310Ed25519()
{
    static nRFCrypto_Ed25519 ed25519;
    return ed25519;
}

crypto_hw_result crypto_cc310_ed25519_verify(const uint8_t signature[64], const uint8_t pub[32], const void *msg, size_t len)
{
    concurrency::LockGuard guard(&cc310Lock());
    if (!nRFCrypto.begin())
        return CRYPTO_HW_UNAVAILABLE;
    const bool ok = cc310Ed25519().verify(signature, (const uint8_t *)msg, len, pub);
    const uint32_t err = cc310Ed25519().lastError();
    nRFCrypto.end();
    if (ok)
        return CRYPTO_HW_OK;
    if (err == CRYS_ECEDW_SIGN_VERIFY_FAILED_ERROR)
        return CRYPTO_HW_REJECTED;
    LOG_DEBUG("CC310 Ed25519 verify error 0x%08x, using software", (unsigned int)err);
    return CRYPTO_HW_UNAVAILABLE;
}

static bool baseMulLocked(uint8_t point[32], const uint8_t scalar[32])
{
    if (!nRFCrypto.begin())
        return false;
    const bool ok = cc310Ed25519().scalarMultBase(point, scalar);
    const uint32_t err = cc310Ed25519().lastError();
    nRFCrypto.end();
    if (!ok)
        LOG_WARN("CC310 Ed25519 base point error 0x%08x, using software", (unsigned int)err);
    return ok;
}

// A known answer gates the hardware on first use; a mismatch or any error leaves R to the software.
static bool baseMulSelfTestPassed()
{
    enum SelfTest : uint8_t { Untested, Passed, Failed };
    static SelfTest state = Untested;
    if (state == Untested) {
        // RFC 8032 7.1 TEST 1: its secret scalar a (mod q) times B is its public key.
        static const uint8_t katScalar[32] = {0x7c, 0x2c, 0xac, 0x12, 0xe6, 0x9b, 0xe9, 0x6a, 0xe9, 0x06, 0x50,
                                              0x65, 0x46, 0x23, 0x85, 0xe8, 0xfc, 0xff, 0x27, 0x68, 0xd9, 0x80,
                                              0xc0, 0xa3, 0xa5, 0x20, 0xf0, 0x06, 0x90, 0x4d, 0xe9, 0x0f};
        static const uint8_t katPoint[32] = {0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe,
                                             0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6,
                                             0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a};
        uint8_t out[32];
        const bool ok = baseMulLocked(out, katScalar) && memcmp(out, katPoint, sizeof(out)) == 0;
        state = ok ? Passed : Failed;
        if (!ok)
            LOG_ERROR("CC310 Ed25519 base point self-test failed, signing in software");
    }
    return state == Passed;
}

crypto_hw_result crypto_cc310_ed25519_basemul(uint8_t point[32], const uint8_t scalar[32])
{
    concurrency::LockGuard guard(&cc310Lock());
    return baseMulSelfTestPassed() && baseMulLocked(point, scalar) ? CRYPTO_HW_OK : CRYPTO_HW_UNAVAILABLE;
}
#endif
#endif
#endif
