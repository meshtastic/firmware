#include "crypto_backends.h"

#ifdef CRYPTO_HAS_CC310
#include "concurrency/LockGuard.h"
#include <Adafruit_nRFCrypto.h>
#include <Crypto.h>
#include <nrf_cc310/include/crys_ec_mont_edw_error.h>
#include <nrf_cc310/include/crys_hash.h>
#include <nrf_cc310/include/ssi_aes.h>
#include <string.h>

static constexpr size_t AES_BLOCK = 16;
static constexpr size_t AES_CHUNK = 4 * AES_BLOCK;
static constexpr size_t SHA256_BLOCK = 64;
static constexpr size_t CCM_L = 2;

// Function-local static: at file scope its constructor would keep it out of linker GC.
static concurrency::Lock &cc310Lock()
{
    static concurrency::Lock lock;
    return lock;
}

static bool beginCc310()
{
    for (int attempt = 0; attempt < 3; attempt++) {
        if (nRFCrypto.begin())
            return true;
        delay(10);
    }
    LOG_ERROR("CC310 failed to start");
    return false;
}

// All data goes through these RAM blocks, because the CC310 DMA cannot read flash. A short last
// block is zero-padded, which is CCM's padding and harmless to CTR, whose output is cut to n.
static bool aesBlocks(SaSiAesUserContext_t *ctx, const uint8_t *in, size_t len, uint8_t *out, uint8_t *lastOut)
{
    alignas(4) uint8_t inBuf[AES_CHUNK], outBuf[AES_CHUNK];
    bool ok = true;
    for (size_t off = 0; ok && off < len;) {
        const size_t n = len - off < AES_CHUNK ? len - off : AES_CHUNK;
        const size_t whole = (n + AES_BLOCK - 1) / AES_BLOCK * AES_BLOCK;
        memcpy(inBuf, in + off, n);
        memset(inBuf + n, 0, whole - n);
        ok = SaSi_AesBlock(ctx, inBuf, whole, outBuf) == SASI_OK;
        if (ok && out)
            memcpy(out + off, outBuf, n);
        if (ok && lastOut)
            memcpy(lastOut, outBuf + whole - AES_BLOCK, AES_BLOCK);
        off += n;
    }
    clean(inBuf);
    clean(outBuf);
    return ok;
}

// k must be in RAM and stay valid for the whole operation.
static bool aesStart(SaSiAesUserContext_t *ctx, SaSiAesOperationMode_t mode, uint8_t k[16], const uint8_t iv[16])
{
    uint8_t ivRam[AES_BLOCK];
    memcpy(ivRam, iv, sizeof(ivRam));
    SaSiAesUserKeyData_t keyData = {k, 16};
    return SaSi_AesInit(ctx, SASI_AES_ENCRYPT, mode, SASI_AES_PADDING_NONE) == SASI_OK &&
           SaSi_AesSetKey(ctx, SASI_AES_USER_KEY, &keyData, sizeof(keyData)) == SASI_OK && SaSi_AesSetIv(ctx, ivRam) == SASI_OK;
}

static bool aesFinish(SaSiAesUserContext_t *ctx, bool ok)
{
    uint8_t unused[AES_BLOCK];
    size_t outSize = sizeof(unused);
    ok = ok && SaSi_AesFinish(ctx, 0, unused, 0, unused, &outSize) == SASI_OK;
    SaSi_AesFree(ctx);
    return ok;
}

static bool ctrRun(uint8_t k[16], const uint8_t counter[16], const uint8_t *in, size_t len, uint8_t *out)
{
    SaSiAesUserContext_t ctx;
    const bool ok = aesStart(&ctx, SASI_AES_MODE_CTR, k, counter) && aesBlocks(&ctx, in, len, out, nullptr);
    return aesFinish(&ctx, ok);
}

bool crypto_nrf52_aes128_ctr(const uint8_t key[16], const uint8_t counter[16], const uint8_t *in, size_t len, uint8_t *out)
{
    concurrency::LockGuard guard(&cc310Lock());
    if (!beginCc310())
        return false;
    nRFCrypto_PowerScope power;
    uint8_t k[16];
    memcpy(k, key, sizeof(k));
    const bool ok = ctrRun(k, counter, in, len, out);
    clean(k);
    if (!ok)
        LOG_ERROR("CC310 AES-CTR failed");
    return ok;
}

static bool ccmCtr(uint8_t k[16], const uint8_t nonce[13], uint16_t counter, const uint8_t *in, size_t len, uint8_t *out)
{
    uint8_t a[AES_BLOCK];
    a[0] = CCM_L - 1;
    memcpy(&a[1], nonce, 15 - CCM_L);
    a[14] = counter >> 8;
    a[15] = counter & 0xff;
    return ctrRun(k, a, in, len, out);
}

// CBC-MAC as plain CBC, keeping only the last ciphertext block. Own CCM, not CRYS_AESCCM, which adds 3 KB of flash.
static bool ccmMac(uint8_t k[16], size_t tag_len, const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                   const uint8_t *data, size_t len, uint8_t x[16])
{
    uint8_t head[3 * AES_BLOCK] = {0};
    size_t headLen = AES_BLOCK;
    head[0] = (aad_len ? 0x40 : 0) | (((tag_len - 2) / 2) << 3) | (CCM_L - 1);
    memcpy(&head[1], nonce, 15 - CCM_L);
    head[14] = len >> 8;
    head[15] = len & 0xff;
    if (aad_len) {
        head[17] = aad_len;
        memcpy(&head[18], aad, aad_len);
        headLen = aad_len + 2 > AES_BLOCK ? 3 * AES_BLOCK : 2 * AES_BLOCK;
    }
    static const uint8_t zeroIv[AES_BLOCK] = {0};
    SaSiAesUserContext_t ctx;
    const bool ok = aesStart(&ctx, SASI_AES_MODE_CBC, k, zeroIv) && aesBlocks(&ctx, head, headLen, nullptr, x) &&
                    aesBlocks(&ctx, data, len, nullptr, x);
    clean(head);
    return aesFinish(&ctx, ok);
}

bool crypto_nrf52_aes128_ccm_seal(const uint8_t key[16], const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                                  const uint8_t *in, size_t len, uint8_t *out, uint8_t *tag, size_t tag_len)
{
    concurrency::LockGuard guard(&cc310Lock());
    if (!beginCc310())
        return false;
    nRFCrypto_PowerScope power;
    uint8_t k[16], x[AES_BLOCK], s0[AES_BLOCK] = {0};
    memcpy(k, key, sizeof(k));
    // The MAC runs first so that in == out works.
    const bool ok = ccmMac(k, tag_len, nonce, aad, aad_len, in, len, x) && ccmCtr(k, nonce, 0, s0, sizeof(s0), s0) &&
                    ccmCtr(k, nonce, 1, in, len, out);
    if (ok) {
        for (size_t i = 0; i < tag_len; i++)
            tag[i] = x[i] ^ s0[i];
    } else {
        LOG_ERROR("CC310 AES-CCM seal failed");
    }
    clean(k);
    clean(x);
    clean(s0);
    return ok;
}

bool crypto_nrf52_aes128_ccm_open(const uint8_t key[16], const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                                  const uint8_t *in, size_t len, const uint8_t *tag, size_t tag_len, uint8_t *out)
{
    concurrency::LockGuard guard(&cc310Lock());
    if (!beginCc310())
        return false;
    nRFCrypto_PowerScope power;
    uint8_t k[16], x[AES_BLOCK], s0[AES_BLOCK] = {0};
    memcpy(k, key, sizeof(k));
    bool ok = ccmCtr(k, nonce, 0, s0, sizeof(s0), s0) && ccmCtr(k, nonce, 1, in, len, out) &&
              ccmMac(k, tag_len, nonce, aad, aad_len, out, len, x);
    if (ok) {
        volatile uint8_t diff = 0;
        for (size_t i = 0; i < tag_len; i++)
            diff |= x[i] ^ s0[i] ^ tag[i];
        ok = diff == 0;
    } else {
        LOG_ERROR("CC310 AES-CCM open failed");
    }
    if (!ok)
        clean(out, len);
    clean(k);
    clean(x);
    clean(s0);
    return ok;
}

static bool hashFeed(CRYS_HASHUserContext_t *ctx, uint8_t block[SHA256_BLOCK], size_t &fill, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    while (len) {
        const size_t n = len < SHA256_BLOCK - fill ? len : SHA256_BLOCK - fill;
        memcpy(block + fill, p, n);
        fill += n;
        p += n;
        len -= n;
        if (fill == SHA256_BLOCK) {
            if (CRYS_HASH_Update(ctx, block, SHA256_BLOCK) != CRYS_OK)
                return false;
            fill = 0;
        }
    }
    return true;
}

static bool sha256WithPrefix(const uint8_t *prefix, size_t prefix_len, const crypto_part *parts, size_t num_parts,
                             uint8_t out[32])
{
    CRYS_HASHUserContext_t ctx;
    CRYS_HASH_Result_t result;
    alignas(4) uint8_t block[SHA256_BLOCK];
    size_t fill = 0;
    bool ok = CRYS_HASH_Init(&ctx, CRYS_HASH_SHA256_mode) == CRYS_OK && hashFeed(&ctx, block, fill, prefix, prefix_len);
    for (size_t i = 0; ok && i < num_parts; i++)
        ok = hashFeed(&ctx, block, fill, parts[i].ptr, parts[i].len);
    // Only the final update may be a partial block.
    ok = ok && (fill == 0 || CRYS_HASH_Update(&ctx, block, fill) == CRYS_OK) && CRYS_HASH_Finish(&ctx, result) == CRYS_OK;
    if (ok)
        memcpy(out, result, 32);
    CRYS_HASH_Free(&ctx);
    clean(block);
    clean(result);
    return ok;
}

// SHA has no error return. A random digest fails closed: zeros would derive a known key.
static void hashFailed(uint8_t out[32])
{
    LOG_ERROR("CC310 SHA-256 failed");
    crypto_random(out, 32);
}

void crypto_nrf52_sha256(const crypto_part *parts, size_t num_parts, uint8_t out[32])
{
    bool ok = false;
    {
        concurrency::LockGuard guard(&cc310Lock());
        if (beginCc310()) {
            nRFCrypto_PowerScope power;
            ok = sha256WithPrefix(nullptr, 0, parts, num_parts, out);
        }
    }
    if (!ok)
        hashFailed(out);
}

// HMAC on the SHA-256 path above, not CRYS_HMAC, which would link another driver for the same engine.
void crypto_nrf52_hmac_sha256(const uint8_t *key, size_t key_len, const crypto_part *parts, size_t num_parts, uint8_t out[32])
{
    bool ok = false;
    {
        concurrency::LockGuard guard(&cc310Lock());
        if (beginCc310()) {
            nRFCrypto_PowerScope power;
            uint8_t pad[SHA256_BLOCK] = {0}, inner[32];
            if (key_len > SHA256_BLOCK) {
                const crypto_part keyPart = {key, key_len};
                ok = sha256WithPrefix(nullptr, 0, &keyPart, 1, pad);
            } else {
                memcpy(pad, key, key_len);
                ok = true;
            }
            for (uint8_t &b : pad)
                b ^= 0x36;
            ok = ok && sha256WithPrefix(pad, sizeof(pad), parts, num_parts, inner);
            for (uint8_t &b : pad)
                b ^= 0x36 ^ 0x5c;
            const crypto_part innerPart = {inner, sizeof(inner)};
            ok = ok && sha256WithPrefix(pad, sizeof(pad), &innerPart, 1, out);
            clean(pad);
            clean(inner);
        }
    }
    if (!ok)
        hashFailed(out);
}

#if !(MESHTASTIC_EXCLUDE_PKI)
// One buffer for both curves, since the lock keeps them from running together.
static union {
    CRYS_ECMONT_TempBuff_t mont;
#if !(MESHTASTIC_EXCLUDE_XEDDSA)
    CRYS_ECEDW_TempBuff_t edw;
#endif
} cc310Temp;

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

// An all-zero result is returned as success: the caller's weak-point check rejects it.
bool crypto_nrf52_x25519(uint8_t out[32], const uint8_t clamped_scalar[32], const uint8_t *point)
{
    if (point && !isCanonicalPoint(point))
        return false;
    concurrency::LockGuard guard(&cc310Lock());
    if (!beginCc310())
        return false;
    alignas(4) uint8_t sh[32], sk[32], pk[32];
    size_t shLen = sizeof(sh);
    memcpy(sk, clamped_scalar, sizeof(sk));
    CRYSError_t err;
    {
        nRFCrypto_PowerScope power;
        if (point) {
            memcpy(pk, point, sizeof(pk));
            pk[31] &= 0x7F;
            err = CRYS_ECMONT_Scalarmult(sh, &shLen, sk, sizeof(sk), pk, sizeof(pk), &cc310Temp.mont);
        } else {
            err = CRYS_ECMONT_ScalarmultBase(sh, &shLen, sk, sizeof(sk), &cc310Temp.mont);
        }
    }
    const bool ok = err == CRYS_OK && shLen == sizeof(sh);
    if (ok)
        memcpy(out, sh, sizeof(sh));
    else
        LOG_ERROR("CC310 X25519 error 0x%08x", (unsigned int)err);
    clean(sk);
    clean(sh);
    clean(&cc310Temp, sizeof(cc310Temp));
    return ok;
}

#if !(MESHTASTIC_EXCLUDE_XEDDSA)
static constexpr size_t VERIFY_COPY_MAX = 256;

static CRYSError_t verifyInRam(const uint8_t signature[64], const uint8_t pub[32], const uint8_t *msg, size_t len)
{
    alignas(4) uint8_t sg[64], pk[32];
    memcpy(sg, signature, sizeof(sg));
    memcpy(pk, pub, sizeof(pk));
    nRFCrypto_PowerScope power;
    // Never a null message, even at length 0.
    return CRYS_ECEDW_Verify(sg, sizeof(sg), pk, sizeof(pk), (uint8_t *)(len ? msg : sg), len, &cc310Temp.edw);
}

// Apart so that an aligned RAM message, which is all the firmware passes, doesn't pay for this stack.
static __attribute__((noinline)) CRYSError_t verifyCopied(const uint8_t signature[64], const uint8_t pub[32], const uint8_t *msg,
                                                          size_t len)
{
    alignas(4) uint8_t copy[VERIFY_COPY_MAX];
    memcpy(copy, msg, len);
    return verifyInRam(signature, pub, copy, len);
}

bool crypto_nrf52_ed25519_verify(const uint8_t signature[64], const uint8_t pub[32], const void *msg, size_t len)
{
    const uint8_t *m = (const uint8_t *)msg;
    const bool dmaSafe = len == 0 || (((uintptr_t)m & 3) == 0 && (uintptr_t)m >= 0x20000000);
    if (!dmaSafe && len > VERIFY_COPY_MAX) {
        LOG_ERROR("CC310 Ed25519 verify: %u byte message must be aligned and in RAM", (unsigned int)len);
        return false;
    }
    concurrency::LockGuard guard(&cc310Lock());
    if (!beginCc310())
        return false;
    const CRYSError_t err = dmaSafe ? verifyInRam(signature, pub, m, len) : verifyCopied(signature, pub, m, len);
    if (err != CRYS_OK && err != CRYS_ECEDW_SIGN_VERIFY_FAILED_ERROR)
        LOG_ERROR("CC310 Ed25519 verify error 0x%08x", (unsigned int)err);
    return err == CRYS_OK;
}

// EcEdwScalarMultBase loops once per two scalar bits, so adding 4q gives every scalar 255 bits and one run time.
static bool baseMulLocked(uint8_t point[32], const uint8_t scalar[32])
{
    static const uint32_t fourQ[8] = {0x73d74fb4, 0x60498c69, 0x8bde7359, 0x537be77a,
                                      0x00000000, 0x00000000, 0x00000000, 0x40000000};
    if (scalar[31] >= 0x20)
        return false;
    uint32_t s[8];
    alignas(4) uint32_t x[8], y[8];
    memcpy(s, scalar, sizeof(s));
    uint64_t carry = 0;
    for (int i = 0; i < 8; i++) {
        carry += (uint64_t)s[i] + fourQ[i];
        s[i] = (uint32_t)carry;
        carry >>= 32;
    }
    const void *domain = EcEdwGetDomain25519();
    uint32_t regsCount = 30;
    CRYSError_t err;
    {
        nRFCrypto_PowerScope power;
        err = SaSi_PalMutexLock(&sasiAsymCryptoMutex, 0xFFFFFFFF);
        if (err == CRYS_OK) {
            err = SaSi_PalPowerSaveModeSelect(0);
            if (err == CRYS_OK)
                err = PkaInitPka(((const uint32_t *)domain)[8], 0, &regsCount);
            if (err == CRYS_OK)
                err = EcEdwScalarMultBase(x, y, s, 8, domain);
            PkaFinishAndMutexUnlock(regsCount);
        }
    }
    clean(s);
    if (err != CRYS_OK) {
        LOG_ERROR("CC310 Ed25519 base point error 0x%08x", (unsigned int)err);
        return false;
    }
    y[7] |= x[0] << 31;
    memcpy(point, y, 32);
    clean(x);
    clean(y);
    return true;
}

// EcEdwScalarMultBase is an internal function of a binary library, so a known answer gates it.
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
        // An error says nothing about the answer, so only a wrong answer is final.
        if (!baseMulLocked(out, katScalar))
            return false;
        state = memcmp(out, katPoint, sizeof(out)) == 0 ? Passed : Failed;
        if (state == Failed)
            LOG_ERROR("CC310 Ed25519 base point self-test failed");
    }
    return state == Passed;
}

bool crypto_nrf52_ed25519_basemul(uint8_t point[32], const uint8_t scalar[32])
{
    concurrency::LockGuard guard(&cc310Lock());
    return beginCc310() && baseMulSelfTestPassed() && baseMulLocked(point, scalar);
}
#endif
#endif
#endif
