#include "configuration.h"

#ifdef MESHTASTIC_ENCRYPTED_STORAGE

// Common includes — available for all platform implementations
#include "EncryptedStorage.h"
#include "FSCommon.h"
#include "RTC.h"
#include "SPILock.h"
#include "SafeFile.h"
#include "SecureZero.h"
#include <algorithm>

#ifdef ARCH_NRF52

// nRF52 CC310 hardware crypto
#include <Adafruit_nRFCrypto.h>
#include <nrf.h>

extern "C" {
#include "nrf_cc310/include/crys_hmac.h"
#include "nrf_cc310/include/ssi_aes.h"
#include "nrf_cc310/include/ssi_aes_defs.h"
}

namespace EncryptedStorage
{

// ---------------------------------------------------------------------------
// File paths and domain-separation strings
// ---------------------------------------------------------------------------

static const char *DEK_FILENAME = "/prefs/.dek";
static const char *TOKEN_FILENAME = "/prefs/.unlock_token";
static const char *BACKOFF_FILENAME = "/prefs/.backoff";

// Passphrase-mixed KEK
static const char *KEK_DOMAIN = "meshtastic-tak-kek-v2";
// Ephemeral: FICR-only, used only to wrap DEK inside the unlock token
static const char *EPHEMERAL_KEK_DOMAIN = "meshtastic-tak-ephemeral-v1";
// HMAC auth label for DEK file
static const char *DEK_AUTH_LABEL = "mdek-auth";

// ---------------------------------------------------------------------------
// Module-level key state
// ---------------------------------------------------------------------------

// Passphrase-mixed KEK (v2), derived on provision/unlock
static uint8_t kek[AES_KEY_SIZE];
static bool kekDerived = false;

// FICR-only ephemeral KEK for token wrapping/unwrapping
static uint8_t ephemeralKek[AES_KEY_SIZE];
static bool ephemeralKekDerived = false;

// Data Encryption Key — loaded from /prefs/.dek, never stored in plaintext
static uint8_t dek[AES_KEY_SIZE];
static bool dekLoaded = false;

// Reason the device is currently locked (set in initLocked / readAndConsumeToken)
static const char *lockReason = "not_provisioned";

// Token state after successful unlock — exposed via getBootsRemaining() / getValidUntilEpoch()
static uint8_t s_bootsRemaining = 0;
static uint32_t s_validUntilEpoch = 0;

// Uptime-based session limit. Set by setSession() at successful unlock.
// s_sessionMaxMs = 0 means no limit (token-only enforcement). RAM-only:
// reboot clears these, but readAndConsumeToken() persists the
// sessionMaxSeconds in the token file and re-calls setSession() from
// the token-load path so token-auto-unlocked sessions inherit the
// same cap. consumeSessionBoot() re-arms in place between sessions.
static uint32_t s_sessionMaxMs = 0;
static uint32_t s_sessionStartedMs = 0;

// Backoff state — seconds remaining before next passphrase attempt is allowed
static uint32_t s_backoffSecondsRemaining = 0;

// ---------------------------------------------------------------------------
// Passphrase attempt backoff helpers
// ---------------------------------------------------------------------------

// Returns the delay in seconds for a given number of failed attempts.
// Schedule: 5, 10, 20, 40, 80, 160, 320, 900 (capped)
static uint32_t backoffDelay(uint8_t attempts)
{
    if (attempts == 0)
        return 0;
    uint32_t delay = 5u;
    for (uint8_t i = 1; i < attempts; i++) {
        delay *= 2;
        if (delay >= 900)
            return 900;
    }
    return delay;
}

// RAM-only: millis() at the most recent failed attempt this boot. Lets us
// enforce within-boot backoff without relying on RTC. Reset to 0 each boot.
static uint32_t s_lastFailMillis = 0;

// Forward declarations for the crypto helpers defined further down. The
// backoff section (next) needs them and we want the backoff state next to
// the rest of the boot/state machinery rather than buried after the crypto.
static bool computeHMAC(const uint8_t *key, size_t keyLen, const uint8_t *data, size_t dataLen, uint8_t *hmacOut);
static bool constTimeEq(const uint8_t *a, const uint8_t *b, size_t len);

// HMAC domain label for the backoff file. Distinct from DEK_AUTH_LABEL so a
// cross-file replay (use a DEK-MAC as a backoff-MAC or vice versa) fails.
static const char *BACKOFF_AUTH_LABEL = "backoff-auth";

// Backoff state file format (38 bytes): attempts(1) + bootsSinceFail(1) +
// lastFailEpoch(4) + HMAC-SHA256(ephemeralKEK, BACKOFF_AUTH_LABEL || body)(32)
//
// H4 (audit): MAC the file with the FICR-derived ephemeralKek so an attacker
// who can write LittleFS (DFU file inject, compromised firmware) cannot
// forge a low-attempts file to bypass backoff. Atomic write via SafeFile
// closes the power-glitch-during-write window. Missing/short/MAC-fail are
// all treated as max-attempts (kBackoffMaxAttempts) so a tamper-delete can
// only INCREASE the wait, never decrease it.
//
// bootsSinceFail is incremented once per boot in initLocked() (saturating at
// 255). It provides a reliable monotonic across-reboot counter for backoff
// enforcement even when the RTC is unset, closing the reboot-bypass: an
// attacker who reboots between failed attempts cannot fast-forward through
// the backoff window because each reboot costs ~3-5 s of nRF52 boot time
// and only advances bootsSinceFail by 1.
static constexpr size_t BACKOFF_BODY_SIZE = 6;
static constexpr size_t BACKOFF_SIZE = BACKOFF_BODY_SIZE + HMAC_SIZE; // 38 bytes
static constexpr uint8_t kBackoffMaxAttempts = 255;

// Compute HMAC-SHA256(ephemeralKek, "backoff-auth" || body) into `out`.
// Caller must already hold CC310 (nRFCrypto.begin/end).
static bool computeBackoffHmac(const uint8_t body[BACKOFF_BODY_SIZE], uint8_t out[HMAC_SIZE])
{
    if (!ephemeralKekDerived)
        return false;
    size_t labelLen = strlen(BACKOFF_AUTH_LABEL);
    meshtastic_security::ZeroizingBuffer<32 + BACKOFF_BODY_SIZE> input; // labelLen <= 32
    memcpy(input.data(), BACKOFF_AUTH_LABEL, labelLen);
    memcpy(input.data() + labelLen, body, BACKOFF_BODY_SIZE);
    return computeHMAC(ephemeralKek, AES_KEY_SIZE, input.data(), labelLen + BACKOFF_BODY_SIZE, out);
}

static void readBackoff(uint8_t &attempts, uint8_t &bootsSinceFail, uint32_t &lastFailEpoch)
{
    // Default outputs: zero-attempts. Reassigned to "max" below if the file
    // is missing OR present-but-tampered. The fresh-device (pre-provision)
    // case is handled by bumpBootsSinceFailOnBoot's early-return; once
    // provision has run, the file is always present and a missing file
    // means something hostile deleted it.
    attempts = 0;
    bootsSinceFail = 0;
    lastFailEpoch = 0;
#ifdef FSCom
    meshtastic_security::ZeroizingBuffer<BACKOFF_SIZE> buf;
    {
        concurrency::LockGuard g(spiLock);
        auto f = FSCom.open(BACKOFF_FILENAME, FILE_O_READ);
        if (!f) {
            // Fresh device (no provision yet) OR an attacker deleted the
            // file. Caller resolves the ambiguity via isProvisioned() —
            // see bumpBootsSinceFailOnBoot and the unlock backoff gate.
            return;
        }
        size_t sz = f.size();
        if (sz != BACKOFF_SIZE) {
            f.close();
            attempts = kBackoffMaxAttempts;
            return;
        }
        size_t n = f.read(buf.data(), BACKOFF_SIZE);
        f.close();
        if (n != BACKOFF_SIZE) {
            attempts = kBackoffMaxAttempts;
            return;
        }
    }
    // Verify HMAC under lock-free CC310 access (we hold no spiLock here).
    uint8_t expected[HMAC_SIZE];
    nRFCrypto.begin();
    bool ok = computeBackoffHmac(buf.data(), expected);
    nRFCrypto.end();
    if (!ok || !constTimeEq(expected, buf.data() + BACKOFF_BODY_SIZE, HMAC_SIZE)) {
        // Tampered or attacker-rewritten file. Fail closed.
        attempts = kBackoffMaxAttempts;
        return;
    }
    attempts = buf.data()[0];
    bootsSinceFail = buf.data()[1];
    memcpy(&lastFailEpoch, buf.data() + 2, 4);
#endif
}

static void writeBackoff(uint8_t attempts, uint8_t bootsSinceFail, uint32_t lastFailEpoch)
{
#ifdef FSCom
    meshtastic_security::ZeroizingBuffer<BACKOFF_SIZE> buf;
    buf.data()[0] = attempts;
    buf.data()[1] = bootsSinceFail;
    memcpy(buf.data() + 2, &lastFailEpoch, 4);
    uint8_t mac[HMAC_SIZE];
    nRFCrypto.begin();
    bool ok = computeBackoffHmac(buf.data(), mac);
    nRFCrypto.end();
    if (!ok) {
        LOG_ERROR("EncryptedStorage: backoff HMAC compute failed");
        return;
    }
    memcpy(buf.data() + BACKOFF_BODY_SIZE, mac, HMAC_SIZE);
    SafeFile sf(BACKOFF_FILENAME, /*fullAtomic=*/true);
    sf.write(buf.data(), BACKOFF_SIZE);
    if (!sf.close()) {
        LOG_ERROR("EncryptedStorage: backoff atomic write failed");
    }
#endif
}

// Called once per boot from initLocked(). Skip the bump on a fresh
// (un-provisioned) device — there's no backoff file to MAC against yet and
// readBackoff would return kBackoffMaxAttempts which would be wrong here.
static void bumpBootsSinceFailOnBoot()
{
    if (!isProvisioned())
        return;
    uint8_t attempts;
    uint8_t bootsSinceFail;
    uint32_t lastFailEpoch;
    readBackoff(attempts, bootsSinceFail, lastFailEpoch);
    if (attempts == 0 || attempts == kBackoffMaxAttempts)
        return;
    if (bootsSinceFail < 255)
        bootsSinceFail++;
    writeBackoff(attempts, bootsSinceFail, lastFailEpoch);
}

// On successful unlock, write a freshly-MAC'd attempts=0 sentinel so the
// file always exists post-provision. Missing == hostile delete from there
// on. (Removing the file instead would make "missing == fresh-cleared" and
// re-open the delete-to-reset bypass that H4 exists to close.)
static void clearBackoff()
{
    writeBackoff(0, 0, 0);
    s_backoffSecondsRemaining = 0;
    s_lastFailMillis = 0;
}

// ---------------------------------------------------------------------------
// Internal helpers: FICR data extraction
// ---------------------------------------------------------------------------

static void readFICR(uint8_t efuseData[16])
{
    // Copy FICR registers to local vars before memcpy (registers are volatile)
    uint32_t tmp;
    tmp = NRF_FICR->DEVICEID[0];
    memcpy(efuseData, &tmp, 4);
    tmp = NRF_FICR->DEVICEID[1];
    memcpy(efuseData + 4, &tmp, 4);
    tmp = NRF_FICR->DEVICEADDR[0];
    memcpy(efuseData + 8, &tmp, 4);
    tmp = NRF_FICR->DEVICEADDR[1];
    memcpy(efuseData + 12, &tmp, 4);
}

// ---------------------------------------------------------------------------
// Internal helpers: CC310 crypto primitives
// ---------------------------------------------------------------------------

/// AES-128-CTR encrypt/decrypt (symmetric). Caller holds CC310.
static bool aesCtr128(const uint8_t *key, const uint8_t *nonce, size_t nonceLen, const uint8_t *input, size_t inputLen,
                      uint8_t *output)
{
    if (inputLen == 0)
        return true;

    SaSiAesUserContext_t ctx;
    SaSiAesUserKeyData_t keyData;
    SaSiAesIv_t iv;

    memset(iv, 0, sizeof(iv));
    size_t copyLen = (nonceLen < sizeof(iv)) ? nonceLen : sizeof(iv);
    memcpy(iv, nonce, copyLen);

    SaSiError_t err = SaSi_AesInit(&ctx, SASI_AES_ENCRYPT, SASI_AES_MODE_CTR, SASI_AES_PADDING_NONE);
    if (err != 0) {
        LOG_ERROR("EncryptedStorage: AES init failed: 0x%x", err);
        return false;
    }

    keyData.pKey = (uint8_t *)key;
    keyData.keySize = AES_KEY_SIZE;
    err = SaSi_AesSetKey(&ctx, SASI_AES_USER_KEY, &keyData, sizeof(keyData));
    if (err != 0) {
        LOG_ERROR("EncryptedStorage: AES setkey failed: 0x%x", err);
        SaSi_AesFree(&ctx);
        return false;
    }

    err = SaSi_AesSetIv(&ctx, iv);
    if (err != 0) {
        LOG_ERROR("EncryptedStorage: AES setiv failed: 0x%x", err);
        SaSi_AesFree(&ctx);
        return false;
    }

    size_t processed = 0;
    size_t fullBlocks = (inputLen / AES_BLOCK_SIZE) * AES_BLOCK_SIZE;
    if (fullBlocks > 0) {
        err = SaSi_AesBlock(&ctx, (uint8_t *)input, fullBlocks, output);
        if (err != 0) {
            LOG_ERROR("EncryptedStorage: AES block failed: 0x%x", err);
            SaSi_AesFree(&ctx);
            return false;
        }
        processed = fullBlocks;
    }

    size_t remaining = inputLen - processed;
    size_t finishOutSize = remaining;
    err = SaSi_AesFinish(&ctx, remaining, (uint8_t *)input + processed, remaining, output + processed, &finishOutSize);
    if (err != 0) {
        LOG_ERROR("EncryptedStorage: AES finish failed: 0x%x", err);
        SaSi_AesFree(&ctx);
        return false;
    }

    SaSi_AesFree(&ctx);
    return true;
}

/// Compute HMAC-SHA256(key, data). Caller holds CC310.
static bool computeHMAC(const uint8_t *key, size_t keyLen, const uint8_t *data, size_t dataLen, uint8_t *hmacOut)
{
    CRYS_HASH_Result_t hmacResult;
    CRYSError_t err = CRYS_HMAC(CRYS_HASH_SHA256_mode, (uint8_t *)key, (uint16_t)keyLen, (uint8_t *)data, dataLen, hmacResult);
    if (err != 0) {
        LOG_ERROR("EncryptedStorage: CRYS_HMAC failed: 0x%x", err);
        return false;
    }
    memcpy(hmacOut, hmacResult, HMAC_SIZE);
    return true;
}

/// Constant-time memory comparison (avoids timing side-channels on HMAC compare).
static bool constTimeEq(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++)
        diff |= a[i] ^ b[i];
    return diff == 0;
}

// ---------------------------------------------------------------------------
// Internal helpers: KEK derivation
// ---------------------------------------------------------------------------

/**
 * Derive the passphrase-mixed KEK and store in module-level kek[].
 * SHA-256("device-efuse-data" || FICR_16 || passphrase || KEK_DOMAIN) → first 16 bytes.
 * Caller must hold CC310 (nRFCrypto.begin()).
 */
static bool deriveKEK(const uint8_t *passphrase, size_t passphraseLen)
{
    uint8_t efuseData[16];
    readFICR(efuseData);

    static const char *prefix = "device-efuse-data";
    uint8_t sha256Result[32];

    nRFCrypto_Hash hash;
    if (!hash.begin(CRYS_HASH_SHA256_mode)) {
        LOG_ERROR("EncryptedStorage: SHA-256 init failed (KEK)");
        meshtastic_security::secure_zero(efuseData, sizeof(efuseData));
        return false;
    }
    hash.update((uint8_t *)prefix, strlen(prefix));
    hash.update(efuseData, sizeof(efuseData));
    hash.update((uint8_t *)passphrase, passphraseLen);
    hash.update((uint8_t *)KEK_DOMAIN, strlen(KEK_DOMAIN));
    hash.end(sha256Result);

    memcpy(kek, sha256Result, AES_KEY_SIZE);
    meshtastic_security::secure_zero(sha256Result, sizeof(sha256Result));
    meshtastic_security::secure_zero(efuseData, sizeof(efuseData));

    kekDerived = true;
    return true;
}

/**
 * Derive the ephemeral KEK (FICR-only, separate domain) into ephemeralKek[].
 * Used only for wrapping/unwrapping the unlock token.
 * Caller must hold CC310.
 */
static bool deriveEphemeralKEK()
{
    if (ephemeralKekDerived)
        return true;

    uint8_t efuseData[16];
    readFICR(efuseData);

    static const char *prefix = "device-efuse-data";
    uint8_t sha256Result[32];

    nRFCrypto_Hash hash;
    if (!hash.begin(CRYS_HASH_SHA256_mode)) {
        LOG_ERROR("EncryptedStorage: SHA-256 init failed (ephemeral KEK)");
        meshtastic_security::secure_zero(efuseData, sizeof(efuseData));
        return false;
    }
    hash.update((uint8_t *)prefix, strlen(prefix));
    hash.update(efuseData, sizeof(efuseData));
    hash.update((uint8_t *)EPHEMERAL_KEK_DOMAIN, strlen(EPHEMERAL_KEK_DOMAIN));
    hash.end(sha256Result);

    memcpy(ephemeralKek, sha256Result, AES_KEY_SIZE);
    meshtastic_security::secure_zero(sha256Result, sizeof(sha256Result));
    meshtastic_security::secure_zero(efuseData, sizeof(efuseData));

    ephemeralKekDerived = true;
    return true;
}

// ---------------------------------------------------------------------------
// Internal helpers: DEK file I/O
// ---------------------------------------------------------------------------

/**
 * Load DEK from the DEK file using the current kek[].
 * Verifies HMAC before returning the DEK.
 */
static bool loadDEK()
{
#ifdef FSCom
    concurrency::LockGuard g(spiLock);
    auto f = FSCom.open(DEK_FILENAME, FILE_O_READ);
    if (!f)
        return false;

    size_t fileSize = f.size();
    if (fileSize != DEK_SIZE) {
        f.close();
        return false;
    }

    uint8_t buf[DEK_SIZE];
    size_t bytesRead = f.read(buf, sizeof(buf));
    f.close();

    if (bytesRead != DEK_SIZE) {
        LOG_ERROR("EncryptedStorage: DEK short read");
        return false;
    }

    // Check magic
    uint32_t magic;
    memcpy(&magic, buf, 4);
    if (magic != DEK_MAGIC) {
        LOG_ERROR("EncryptedStorage: DEK bad magic");
        return false;
    }

    uint8_t *nonce = buf + 4;
    uint8_t *encDek = buf + 4 + NONCE_SIZE;

    // Verify HMAC-SHA256(KEK, DEK_AUTH_LABEL || nonce || encDEK)
    size_t authLabelLen = strlen(DEK_AUTH_LABEL);
    size_t hmacInputLen = authLabelLen + NONCE_SIZE + AES_KEY_SIZE;
    auto hmacInput = meshtastic_security::make_zeroizing_array(hmacInputLen);
    if (!hmacInput) {
        LOG_ERROR("EncryptedStorage: OOM for DEK HMAC verify");
        return false;
    }
    memcpy(hmacInput.get(), DEK_AUTH_LABEL, authLabelLen);
    memcpy(hmacInput.get() + authLabelLen, nonce, NONCE_SIZE);
    memcpy(hmacInput.get() + authLabelLen + NONCE_SIZE, encDek, AES_KEY_SIZE);

    meshtastic_security::ZeroizingBuffer<HMAC_SIZE> expectedHmac;
    nRFCrypto.begin();
    bool hmacOk = computeHMAC(kek, AES_KEY_SIZE, hmacInput.get(), hmacInputLen, expectedHmac.data());
    nRFCrypto.end();
    hmacInput.reset();

    if (!hmacOk) {
        return false;
    }

    const uint8_t *storedHmac = buf + DEK_SIZE - HMAC_SIZE;
    if (!constTimeEq(expectedHmac.data(), storedHmac, HMAC_SIZE)) {
        LOG_ERROR("EncryptedStorage: DEK HMAC mismatch — wrong passphrase or tampered file");
        return false;
    }

    // Decrypt DEK into a local candidate — only write to global dek[] on success so that
    // a wrong passphrase attempt does not destroy the live DEK in RAM.
    meshtastic_security::ZeroizingBuffer<AES_KEY_SIZE> dekCandidate;
    nRFCrypto.begin();
    bool decOk = aesCtr128(kek, nonce, NONCE_SIZE, encDek, AES_KEY_SIZE, dekCandidate.data());
    nRFCrypto.end();

    if (!decOk) {
        LOG_ERROR("EncryptedStorage: DEK decrypt failed");
        return false;
    }

    memcpy(dek, dekCandidate.data(), AES_KEY_SIZE);

    LOG_INFO("EncryptedStorage: DEK loaded and verified");
    return true;
#else
    return false;
#endif
}

/**
 * Save the current in-RAM dek[] to disk as a DEK file, wrapped with kek[].
 * Overwrites any existing DEK file.
 */
static bool saveDEK()
{
#ifdef FSCom
    // Generate random nonce
    uint8_t nonce[NONCE_SIZE];
    nRFCrypto.begin();
    if (!nRFCrypto.Random.generate(nonce, NONCE_SIZE)) {
        LOG_ERROR("EncryptedStorage: TRNG failed for DEK nonce");
        nRFCrypto.end();
        return false;
    }

    // Encrypt DEK with KEK
    uint8_t encDek[AES_KEY_SIZE];
    bool encOk = aesCtr128(kek, nonce, NONCE_SIZE, dek, AES_KEY_SIZE, encDek);
    if (!encOk) {
        LOG_ERROR("EncryptedStorage: DEK encrypt failed");
        nRFCrypto.end();
        meshtastic_security::secure_zero(encDek, sizeof(encDek));
        return false;
    }

    // Compute HMAC-SHA256(KEK, DEK_AUTH_LABEL || nonce || encDEK)
    size_t authLabelLen = strlen(DEK_AUTH_LABEL);
    size_t hmacInputLen = authLabelLen + NONCE_SIZE + AES_KEY_SIZE;
    auto hmacInput = meshtastic_security::make_zeroizing_array(hmacInputLen);
    if (!hmacInput) {
        LOG_ERROR("EncryptedStorage: OOM for DEK HMAC");
        nRFCrypto.end();
        return false;
    }
    memcpy(hmacInput.get(), DEK_AUTH_LABEL, authLabelLen);
    memcpy(hmacInput.get() + authLabelLen, nonce, NONCE_SIZE);
    memcpy(hmacInput.get() + authLabelLen + NONCE_SIZE, encDek, AES_KEY_SIZE);

    uint8_t hmac[HMAC_SIZE];
    bool hmacOk = computeHMAC(kek, AES_KEY_SIZE, hmacInput.get(), hmacInputLen, hmac);
    nRFCrypto.end();
    hmacInput.reset();

    if (!hmacOk) {
        meshtastic_security::secure_zero(encDek, sizeof(encDek));
        return false;
    }

    // Write file: magic(4)+nonce(13)+encDEK(16)+hmac(32) = 65 bytes
    // H12 (audit): atomic write via SafeFile. Power-loss between remove()
    // and write() previously left a missing or partial DEK file, which
    // bricked the device — the encrypted protos can't be decrypted with
    // no DEK on flash. SafeFile writes a tmp file, reads it back to verify
    // a content hash, then atomically renames over the target. Crash before
    // rename → old DEK stays in place; crash after rename → new DEK is on
    // disk and verified.
    uint32_t magic = DEK_MAGIC;
    SafeFile sf(DEK_FILENAME, /*fullAtomic=*/true);
    sf.write((uint8_t *)&magic, 4);
    sf.write(nonce, NONCE_SIZE);
    sf.write(encDek, AES_KEY_SIZE);
    sf.write(hmac, HMAC_SIZE);
    bool ok = sf.close();

    meshtastic_security::secure_zero(nonce, sizeof(nonce));
    meshtastic_security::secure_zero(encDek, sizeof(encDek));
    meshtastic_security::secure_zero(hmac, sizeof(hmac));

    if (!ok) {
        LOG_ERROR("EncryptedStorage: DEK write/verify failed");
        return false;
    }
    LOG_INFO("EncryptedStorage: DEK saved");
    return true;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Internal helpers: unlock token I/O
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// M4 (audit): monotonic counter for token rollback protection
// ---------------------------------------------------------------------------
//
// Each new unlock token carries a uint32 counter inside its MAC'd body that
// strictly increases over the device's lifetime. The highest counter we've
// ever seen is persisted to /prefs/.tokmono and MAC'd with the FICR-only
// ephemeralKek under a distinct domain label, so a casual flash-write
// attacker (no flash extraction, no FICR access) cannot forge it.
//
// Rollback attempt: attacker captures token T1 at time T, operator unlocks
// later (token T2, counter > T1), attacker writes T1 back. readAndConsume
// sees T1.counter < max_seen and rejects as rollback.

static const char *MONO_FILENAME = "/prefs/.tokmono";
static const char *MONO_AUTH_LABEL = "tokmono-auth";
static constexpr size_t MONO_BODY_SIZE = 4;
static constexpr size_t MONO_TOTAL_SIZE = MONO_BODY_SIZE + HMAC_SIZE; // 36 bytes

// Compute HMAC-SHA256(ephemeralKek, MONO_AUTH_LABEL || body). Caller holds
// CC310 (nRFCrypto.begin/end).
static bool computeMonoHmac(const uint8_t body[MONO_BODY_SIZE], uint8_t out[HMAC_SIZE])
{
    if (!ephemeralKekDerived)
        return false;
    size_t labelLen = strlen(MONO_AUTH_LABEL);
    meshtastic_security::ZeroizingBuffer<32 + MONO_BODY_SIZE> input;
    memcpy(input.data(), MONO_AUTH_LABEL, labelLen);
    memcpy(input.data() + labelLen, body, MONO_BODY_SIZE);
    return computeHMAC(ephemeralKek, AES_KEY_SIZE, input.data(), labelLen + MONO_BODY_SIZE, out);
}

// Read the persisted max-counter-seen value. Missing/short/MAC-fail
// returns 0 — the safe default that lets the next token write succeed and
// re-seed the file. Unlike the backoff file, missing-here is not a tamper
// signal: a fresh device or a device whose .tokmono got wiped (e.g. via
// factory-erase) legitimately has no counter file.
static uint32_t readMonoCounter()
{
#ifdef FSCom
    meshtastic_security::ZeroizingBuffer<MONO_TOTAL_SIZE> buf;
    {
        concurrency::LockGuard g(spiLock);
        auto f = FSCom.open(MONO_FILENAME, FILE_O_READ);
        if (!f)
            return 0;
        size_t sz = f.size();
        if (sz != MONO_TOTAL_SIZE) {
            f.close();
            return 0;
        }
        size_t n = f.read(buf.data(), MONO_TOTAL_SIZE);
        f.close();
        if (n != MONO_TOTAL_SIZE)
            return 0;
    }
    uint8_t expected[HMAC_SIZE];
    nRFCrypto.begin();
    bool ok = computeMonoHmac(buf.data(), expected);
    nRFCrypto.end();
    if (!ok || !constTimeEq(expected, buf.data() + MONO_BODY_SIZE, HMAC_SIZE))
        return 0;
    uint32_t counter;
    memcpy(&counter, buf.data(), 4);
    return counter;
#else
    return 0;
#endif
}

// Persist a new max-counter-seen value. Best-effort: log on failure but do
// not abort the caller (the token write that incremented the counter has
// already committed; a missing/stale .tokmono on the next read will be
// quietly promoted by readAndConsumeToken when it sees a token whose
// counter exceeds the persisted value).
static bool writeMonoCounter(uint32_t counter)
{
#ifdef FSCom
    meshtastic_security::ZeroizingBuffer<MONO_TOTAL_SIZE> buf;
    memcpy(buf.data(), &counter, 4);
    uint8_t mac[HMAC_SIZE];
    nRFCrypto.begin();
    bool ok = computeMonoHmac(buf.data(), mac);
    nRFCrypto.end();
    if (!ok) {
        LOG_ERROR("EncryptedStorage: mono-counter HMAC failed");
        return false;
    }
    memcpy(buf.data() + MONO_BODY_SIZE, mac, HMAC_SIZE);
    SafeFile sf(MONO_FILENAME, /*fullAtomic=*/true);
    sf.write(buf.data(), MONO_TOTAL_SIZE);
    if (!sf.close()) {
        LOG_ERROR("EncryptedStorage: mono-counter atomic write failed");
        return false;
    }
    return true;
#else
    return false;
#endif
}

/**
 * Write a new unlock token to TOKEN_FILENAME.
 * Wraps the current in-RAM dek[] with ephemeralKek[].
 * @param bootsRemaining     Number of boots this token grants
 * @param validUntilEpoch    Unix timestamp after which token is invalid (0 = no limit)
 * @param sessionMaxSeconds  Uptime-based session cap per boot (0 = no cap).
 *                           Persisted in the token so token-auto-unlock at
 *                           cold boot inherits the same limit. Reboot
 *                           starts a fresh session window — combined with
 *                           bootsRemaining, gives a hard exposure ceiling
 *                           bootsRemaining * sessionMaxSeconds.
 */
static bool writeUnlockToken(uint8_t bootsRemaining, uint32_t validUntilEpoch, uint32_t sessionMaxSeconds)
{
#ifdef FSCom
    uint8_t nonce[NONCE_SIZE];
    nRFCrypto.begin();
    if (!nRFCrypto.Random.generate(nonce, NONCE_SIZE)) {
        LOG_ERROR("EncryptedStorage: TRNG failed for token nonce");
        nRFCrypto.end();
        return false;
    }

    uint8_t encDek[AES_KEY_SIZE];
    bool encOk = aesCtr128(ephemeralKek, nonce, NONCE_SIZE, dek, AES_KEY_SIZE, encDek);
    if (!encOk) {
        LOG_ERROR("EncryptedStorage: Token DEK encrypt failed");
        nRFCrypto.end();
        meshtastic_security::secure_zero(encDek, sizeof(encDek));
        return false;
    }

    // M4 (audit): claim a fresh monotonic-counter slot ABOVE the highest
    // value previously persisted. The new counter is MAC'd into the token
    // body below; after the token write succeeds we persist this value to
    // /prefs/.tokmono so the next readAndConsumeToken can reject any older
    // token that gets restored to disk later.
    uint32_t newMonoCounter = readMonoCounter() + 1;

    // Build body for HMAC (everything before the trailing HMAC)
    uint8_t body[TOKEN_BODY_SIZE];
    size_t pos = 0;
    uint32_t magic = TOKEN_MAGIC;
    memcpy(body + pos, &magic, 4);
    pos += 4;
    memcpy(body + pos, nonce, NONCE_SIZE);
    pos += NONCE_SIZE;
    memcpy(body + pos, encDek, AES_KEY_SIZE);
    pos += AES_KEY_SIZE;
    body[pos++] = bootsRemaining;
    memcpy(body + pos, &validUntilEpoch, 4);
    pos += 4;
    memcpy(body + pos, &sessionMaxSeconds, 4);
    pos += 4;
    memcpy(body + pos, &newMonoCounter, 4);
    pos += 4;

    uint8_t hmac[HMAC_SIZE];
    bool hmacOk = computeHMAC(ephemeralKek, AES_KEY_SIZE, body, TOKEN_BODY_SIZE, hmac);
    nRFCrypto.end();

    meshtastic_security::secure_zero(encDek, sizeof(encDek));

    if (!hmacOk) {
        meshtastic_security::secure_zero(body, sizeof(body));
        return false;
    }

    // H12 (audit): atomic token write via SafeFile (see saveDEK note for
    // the same rationale). Power-loss between remove and write previously
    // left the token in an unreadable state, forcing the operator to re-
    // enter the passphrase from a client. SafeFile rolls back to the
    // previous token if the new write fails verification.
    SafeFile sf(TOKEN_FILENAME, /*fullAtomic=*/true);
    sf.write(body, TOKEN_BODY_SIZE);
    sf.write(hmac, HMAC_SIZE);
    bool tokOk = sf.close();
    if (!tokOk) {
        LOG_ERROR("EncryptedStorage: token write/verify failed");
        meshtastic_security::secure_zero(body, sizeof(body));
        meshtastic_security::secure_zero(hmac, sizeof(hmac));
        return false;
    }

    meshtastic_security::secure_zero(body, sizeof(body));
    meshtastic_security::secure_zero(hmac, sizeof(hmac));

    // M4: persist new max-counter-seen AFTER the token write committed.
    // If this write fails the token is still valid (its counter is
    // greater than the persisted value); readAndConsumeToken will
    // promote .tokmono on the next read.
    if (!writeMonoCounter(newMonoCounter)) {
        LOG_WARN("EncryptedStorage: mono-counter persist failed (will self-heal on next read)");
    }

    LOG_INFO("EncryptedStorage: Unlock token written (boots=%d, epoch=%u, mono=%u)", bootsRemaining, validUntilEpoch,
             (unsigned)newMonoCounter);
    return true;
#else
    return false;
#endif
}

/**
 * Read, validate, and consume the unlock token.
 * If valid: decrypts DEK into dek[], decrements boot count, rewrites token (or deletes if boots==0).
 * Returns true if the token was valid and DEK was loaded.
 */
static bool readAndConsumeToken()
{
#ifdef FSCom
    // Read the token file. M10 (audit): the 74-byte buffer holds the entire
    // wrapped DEK + HMAC; using ZeroizingBuffer ensures the destructor
    // wipes it on every return path (success and all the error cases
    // below) without needing one secure_zero per goto-label.
    meshtastic_security::ZeroizingBuffer<TOKEN_TOTAL_SIZE> buf;
    {
        concurrency::LockGuard g(spiLock);
        auto f = FSCom.open(TOKEN_FILENAME, FILE_O_READ);
        if (!f)
            return false;

        size_t fileSize = f.size();
        if (fileSize != TOKEN_TOTAL_SIZE) {
            f.close();
            LOG_WARN("EncryptedStorage: Token file wrong size (%d), deleting", fileSize);
            FSCom.remove(TOKEN_FILENAME);
            lockReason = "token_wrong_size";
            return false;
        }

        size_t bytesRead = f.read(buf.data(), TOKEN_TOTAL_SIZE);
        f.close();

        if (bytesRead != TOKEN_TOTAL_SIZE) {
            LOG_ERROR("EncryptedStorage: Token short read");
            FSCom.remove(TOKEN_FILENAME);
            return false;
        }
    }

    // Verify magic
    uint32_t magic;
    memcpy(&magic, buf.data(), 4);
    if (magic != TOKEN_MAGIC) {
        LOG_ERROR("EncryptedStorage: Token bad magic, deleting");
        concurrency::LockGuard g(spiLock);
        FSCom.remove(TOKEN_FILENAME);
        lockReason = "token_bad_magic";
        return false;
    }

    // Verify HMAC-SHA256(ephemeralKek, body). M10: ZeroizingBuffer wipes on scope exit.
    meshtastic_security::ZeroizingBuffer<HMAC_SIZE> computedHmac;
    nRFCrypto.begin();
    bool hmacOk = computeHMAC(ephemeralKek, AES_KEY_SIZE, buf.data(), TOKEN_BODY_SIZE, computedHmac.data());
    nRFCrypto.end();

    if (!hmacOk || !constTimeEq(computedHmac.data(), buf.data() + TOKEN_BODY_SIZE, HMAC_SIZE)) {
        LOG_ERROR("EncryptedStorage: Token HMAC failed — tampered or wrong device, deleting");
        concurrency::LockGuard g(spiLock);
        FSCom.remove(TOKEN_FILENAME);
        lockReason = "token_hmac_fail";
        return false;
    }

    // Parse fields from body
    size_t pos = 4; // skip magic
    const uint8_t *nonce = buf.data() + pos;
    pos += NONCE_SIZE;
    const uint8_t *encDek = buf.data() + pos;
    pos += AES_KEY_SIZE;
    uint8_t bootsRemaining = buf[pos++];
    uint32_t validUntilEpoch;
    memcpy(&validUntilEpoch, buf.data() + pos, 4);
    pos += 4;
    uint32_t sessionMaxSeconds;
    memcpy(&sessionMaxSeconds, buf.data() + pos, 4);
    pos += 4;
    uint32_t tokenMonoCounter;
    memcpy(&tokenMonoCounter, buf.data() + pos, 4);

    // M4 (audit): reject any token whose monotonic counter is below the
    // persisted max-seen. An attacker who once read disk could otherwise
    // restore a higher-bootcount / weaker-policy token even after the
    // operator unlocked again with tighter parameters; this check makes
    // such a restore visible and fatal at boot.
    //
    // If the token's counter is GREATER than what we've persisted (e.g.
    // the .tokmono file was lost via factory-erase, or the persist after
    // a token write itself failed), accept and promote .tokmono to the
    // current value. Equal is the normal case post-write.
    uint32_t maxSeenCounter = readMonoCounter();
    if (tokenMonoCounter < maxSeenCounter) {
        LOG_ERROR("EncryptedStorage: Token rollback detected (counter=%u, max-seen=%u), deleting", (unsigned)tokenMonoCounter,
                  (unsigned)maxSeenCounter);
        concurrency::LockGuard g(spiLock);
        FSCom.remove(TOKEN_FILENAME);
        lockReason = "token_rollback";
        return false;
    }
    if (tokenMonoCounter > maxSeenCounter) {
        // Self-heal: this token is newer than what we knew, promote it.
        writeMonoCounter(tokenMonoCounter);
    }

    // Check boot count
    if (bootsRemaining == 0) {
        LOG_WARN("EncryptedStorage: Token boot count exhausted, deleting");
        concurrency::LockGuard g(spiLock);
        FSCom.remove(TOKEN_FILENAME);
        lockReason = "token_boots_zero";
        return false;
    }

    // Check time expiry. A wall-clock TTL (validUntilEpoch != 0) needs a
    // currently valid RTC to verify. getValidTime() returns 0 unless we
    // actually have an RTC source — getTime() would return a boot-relative
    // count, which an attacker can reset by power-cycling with no RTC sync.
    //
    // If the wall-clock TTL is set but we can't verify it right now:
    //   - boot count still has budget  -> fall back to the boot-count TTL,
    //     keep the token. The boot count is independently verifiable
    //     without an RTC, so the token is not unbounded.
    //   - boot count is the only thing we had and it's zero  -> already
    //     rejected above. (validUntilEpoch is never the *sole* TTL here:
    //     bootsRemaining > 0 is guaranteed by the check above.)
    // We only hard-reject (delete) a token whose wall-clock TTL we *can*
    // evaluate and find expired.
    if (validUntilEpoch != 0) {
        uint32_t now = getValidTime(RTCQualityDevice);
        if (now == 0) {
            LOG_WARN("EncryptedStorage: Token wall-clock TTL unverifiable (no RTC), falling back to boot count (%u left)",
                     bootsRemaining);
        } else if (now > validUntilEpoch) {
            LOG_WARN("EncryptedStorage: Token expired (now=%u, until=%u), deleting", now, validUntilEpoch);
            concurrency::LockGuard g(spiLock);
            FSCom.remove(TOKEN_FILENAME);
            lockReason = "token_expired";
            return false;
        }
    }

    // Decrypt DEK from token
    nRFCrypto.begin();
    bool decOk = aesCtr128(ephemeralKek, nonce, NONCE_SIZE, encDek, AES_KEY_SIZE, dek);
    nRFCrypto.end();

    if (!decOk) {
        LOG_ERROR("EncryptedStorage: Token DEK decrypt failed");
        meshtastic_security::secure_zero(dek, sizeof(dek));
        concurrency::LockGuard g(spiLock);
        FSCom.remove(TOKEN_FILENAME);
        lockReason = "token_dek_fail";
        return false;
    }

    // Decrement boot count and rewrite (or delete if now zero)
    uint8_t newBoots = bootsRemaining - 1;
    if (newBoots == 0) {
        LOG_INFO("EncryptedStorage: Token last boot consumed, deleting");
        concurrency::LockGuard g(spiLock);
        FSCom.remove(TOKEN_FILENAME);
    } else {
        writeUnlockToken(newBoots, validUntilEpoch, sessionMaxSeconds);
    }

    dekLoaded = true;
    s_bootsRemaining = newBoots;
    s_validUntilEpoch = validUntilEpoch;
    // Start the session timer if the token carries one. Token-auto-unlocked
    // boots inherit the same cap that was set at passphrase-unlock time.
    setSession(sessionMaxSeconds);
    LOG_INFO("EncryptedStorage: Token valid, DEK loaded (%d boots remaining%s)", newBoots,
             sessionMaxSeconds ? ", session timer armed" : "");
    return true;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Internal helpers: DEK generation
// ---------------------------------------------------------------------------

static bool generateDEK()
{
    nRFCrypto.begin();
    bool ok = nRFCrypto.Random.generate(dek, AES_KEY_SIZE);
    nRFCrypto.end();

    if (!ok) {
        LOG_ERROR("EncryptedStorage: TRNG failed generating DEK");
        meshtastic_security::secure_zero(dek, sizeof(dek));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Public API: passphrase-gated boot
// ---------------------------------------------------------------------------

void initLocked()
{
    nRFCrypto.begin();
    bool ephOk = deriveEphemeralKEK();
    nRFCrypto.end();

    if (!ephOk) {
        LOG_ERROR("EncryptedStorage: Ephemeral KEK derivation failed");
        return;
    }

    // Per-boot increment of bootsSinceFail. Drives the cross-reboot layer of
    // the passphrase-attempt backoff so an attacker can't bypass exponential
    // delays just by power-cycling between attempts.
    bumpBootsSinceFailOnBoot();

    // Attempt to unlock via stored token
    if (!isProvisioned()) {
        lockReason = "not_provisioned";
    } else {
#ifdef FSCom
        {
            concurrency::LockGuard g(spiLock);
            if (!FSCom.exists(TOKEN_FILENAME))
                lockReason = "token_missing";
        }
#endif
    }

    if (readAndConsumeToken()) {
        lockReason = "ok";
        LOG_INFO("EncryptedStorage: Unlocked via token");
        return;
    }

    // Not unlocked — log the device state for the operator
    if (isProvisioned()) {
        LOG_WARN("EncryptedStorage: Device LOCKED — reason: %s", lockReason);
    } else {
        LOG_WARN("EncryptedStorage: Device NOT PROVISIONED — operator must set passphrase");
    }
}

const char *getLockReason()
{
    return lockReason;
}

uint8_t getBootsRemaining()
{
    return s_bootsRemaining;
}

uint32_t getValidUntilEpoch()
{
    return s_validUntilEpoch;
}

uint32_t getBackoffSecondsRemaining()
{
    return s_backoffSecondsRemaining;
}

void setSession(uint32_t maxSeconds)
{
    s_sessionMaxMs = maxSeconds * 1000UL;
    s_sessionStartedMs = millis();
}

bool isSessionExpired()
{
    if (s_sessionMaxMs == 0)
        return false;
    return (millis() - s_sessionStartedMs) > s_sessionMaxMs;
}

uint32_t getSessionRemainingSeconds()
{
    if (s_sessionMaxMs == 0)
        return 0;
    uint32_t elapsedMs = millis() - s_sessionStartedMs;
    if (elapsedMs >= s_sessionMaxMs)
        return 0;
    return (s_sessionMaxMs - elapsedMs) / 1000UL;
}

uint8_t consumeSessionBoot()
{
    if (s_bootsRemaining == 0) {
        // Caller-side error: no budget to consume. Don't touch flash.
        return 0;
    }
    uint8_t newBoots = s_bootsRemaining - 1;
#ifdef FSCom
    if (newBoots == 0) {
        // Last session: delete the on-flash token. The DEK in RAM stays
        // live for this final session; the next session expiry will see
        // s_bootsRemaining == 0 and exhaust into a hard lock + reboot.
        concurrency::LockGuard g(spiLock);
        FSCom.remove(TOKEN_FILENAME);
    } else {
        // Rewrite the token with the new count. Carries the existing
        // validUntilEpoch and the in-RAM sessionMaxSeconds forward so
        // token-auto-unlock on any future reboot honors the same policy.
        writeUnlockToken(newBoots, s_validUntilEpoch, s_sessionMaxMs / 1000UL);
    }
#endif
    s_bootsRemaining = newBoots;
    // Re-arm the uptime session in place. setSession() with the same
    // duration resets s_sessionStartedMs = millis(), starting the next
    // session window without rebooting.
    setSession(s_sessionMaxMs / 1000UL);
    return newBoots;
}

bool provisionPassphrase(const uint8_t *passphrase, size_t passphraseLen, uint8_t bootsRemaining, uint32_t validUntilEpoch,
                         uint32_t sessionMaxSeconds)
{
    // MED-8: proto private_key field is 32 bytes; cap to match (was incorrectly 64)
    if (passphraseLen == 0 || passphraseLen > 32) {
        LOG_ERROR("EncryptedStorage: Invalid passphrase length %d", passphraseLen);
        return false;
    }

    if (!ephemeralKekDerived) {
        nRFCrypto.begin();
        bool ok = deriveEphemeralKEK();
        nRFCrypto.end();
        if (!ok)
            return false;
    }

    if (!generateDEK())
        return false;

    // Derive KEK from passphrase
    nRFCrypto.begin();
    bool kekOk = deriveKEK(passphrase, passphraseLen);
    nRFCrypto.end();
    if (!kekOk) {
        meshtastic_security::secure_zero(dek, sizeof(dek));
        return false;
    }

    // Save the DEK file
    if (!saveDEK()) {
        meshtastic_security::secure_zero(dek, sizeof(dek));
        kekDerived = false;
        return false;
    }

    // Create unlock token (validUntilEpoch is an absolute Unix timestamp from the client; 0 = no limit)
    if (!writeUnlockToken(bootsRemaining, validUntilEpoch, sessionMaxSeconds)) {
        LOG_WARN("EncryptedStorage: Token write failed after provision (continuing unlocked)");
    }

    // H4 (audit): seed an attempts=0 backoff sentinel so the file is
    // present post-provision. From this point on, a missing backoff file
    // means an attacker deleted it — readBackoff returns max-attempts and
    // forces a full backoff window. Without this, the very first failed
    // attempt would find a missing file and have to choose between fresh
    // (no penalty, attacker bypass) and tamper (legitimate user locked
    // out on first typo).
    clearBackoff();

    dekLoaded = true;
    s_bootsRemaining = bootsRemaining;
    s_validUntilEpoch = validUntilEpoch;
    setSession(sessionMaxSeconds);
    LOG_INFO("EncryptedStorage: Provisioning complete");
    return true;
}

bool unlockWithPassphrase(const uint8_t *passphrase, size_t passphraseLen, uint8_t bootsRemaining, uint32_t validUntilEpoch,
                          uint32_t sessionMaxSeconds)
{
    // MED-8: proto private_key field is 32 bytes; cap to match (was incorrectly 64)
    if (passphraseLen == 0 || passphraseLen > 32) {
        LOG_ERROR("EncryptedStorage: Invalid passphrase length %d", passphraseLen);
        return false;
    }

    // Exponential backoff. Three independent enforcement layers — any one
    // triggers a block — so the policy is robust whether RTC is valid, never
    // synced, or being spoofed within reasonable bounds:
    //
    //   1. Within-boot: millis() since the last failure on THIS boot. RAM-only,
    //      reliable, immune to RTC tampering.
    //   2. Wall-clock: lastFailEpoch vs getValidTime(). Only checked when the
    //      RTC is actually valid (getValidTime() returns 0 otherwise) AND the
    //      epoch we persisted at failure was itself valid.
    //   3. Reboot count: bootsSinceFail must be >= ceil(delay / ~5s reboot).
    //      Survives across reboots without any time source; each reboot only
    //      advances it by 1 and costs the attacker boot time (~3-5 s on nRF52).
    {
        uint8_t attempts;
        uint8_t bootsSinceFail;
        uint32_t lastFailEpoch;
        readBackoff(attempts, bootsSinceFail, lastFailEpoch);

        if (attempts > 0) {
            uint32_t delay = backoffDelay(attempts);
            uint32_t maxRemaining = 0;

            // (1) within-boot enforcement using millis()
            if (s_lastFailMillis != 0) {
                uint32_t elapsedSec = (millis() - s_lastFailMillis) / 1000u;
                if (elapsedSec < delay) {
                    uint32_t r = delay - elapsedSec;
                    if (r > maxRemaining)
                        maxRemaining = r;
                }
            }

            // (2) wall-clock enforcement when RTC is actually valid AND the
            // persisted lastFailEpoch was recorded with a valid RTC.
            uint32_t now = getValidTime(RTCQualityDevice);
            if (now != 0 && lastFailEpoch != 0 && now >= lastFailEpoch) {
                uint32_t elapsed = now - lastFailEpoch;
                if (elapsed < delay) {
                    uint32_t r = delay - elapsed;
                    if (r > maxRemaining)
                        maxRemaining = r;
                }
            }

            // (3) reboot-count fallback. Always enforced — closes the bypass
            // where an attacker reboots between attempts (which resets millis
            // and may leave the RTC unsynced). Conservative: assume ~5 s per
            // reboot cycle, so require ceil(delay / 5) boots to elapse.
            uint8_t bootsNeeded = (uint8_t)std::min<uint32_t>(255u, (delay + 4u) / 5u);
            if (bootsNeeded == 0)
                bootsNeeded = 1;
            if (bootsSinceFail < bootsNeeded) {
                // Estimate remaining seconds for client UX: missing boots * 5s.
                uint32_t r = (uint32_t)(bootsNeeded - bootsSinceFail) * 5u;
                if (r > maxRemaining)
                    maxRemaining = r;
            }

            if (maxRemaining > 0) {
                s_backoffSecondsRemaining = maxRemaining;
                LOG_WARN("EncryptedStorage: Passphrase attempt blocked by backoff (~%us remaining)", s_backoffSecondsRemaining);
                return false;
            }
        }
        s_backoffSecondsRemaining = 0;
    }

    if (!ephemeralKekDerived) {
        nRFCrypto.begin();
        bool ok = deriveEphemeralKEK();
        nRFCrypto.end();
        if (!ok)
            return false;
    }

    // Derive KEK
    nRFCrypto.begin();
    bool kekOk = deriveKEK(passphrase, passphraseLen);
    nRFCrypto.end();
    if (!kekOk)
        return false;

    // H3 (audit): RESERVE the attempt slot on disk BEFORE running the HMAC
    // verify. The previous design wrote the failure record only after a
    // failed verify, so pulling power between verify and the write left
    // attempts unchanged — an attacker could glitch the chip mid-call to
    // bypass the counter. Pre-incrementing means the attempt is durably
    // recorded regardless of what happens to the chip during verify; the
    // success path then writes attempts=0 to clear the reservation.
    uint8_t reservedAttempts;
    {
        uint8_t bs;
        uint32_t lfe;
        readBackoff(reservedAttempts, bs, lfe);
        if (reservedAttempts < 255)
            reservedAttempts++;
        uint32_t now = getValidTime(RTCQualityDevice);
        writeBackoff(reservedAttempts, 0, now);
    }
    auto onFailure = [reservedAttempts]() {
        s_lastFailMillis = millis();
        if (s_lastFailMillis == 0)
            s_lastFailMillis = 1; // sentinel: never 0 after a real fail
        s_backoffSecondsRemaining = backoffDelay(reservedAttempts);
        LOG_WARN("EncryptedStorage: Wrong passphrase (attempt %u, next in ~%us)", (unsigned)reservedAttempts,
                 s_backoffSecondsRemaining);
    };

    // Load the DEK (verifies HMAC — wrong passphrase will fail here)
    if (!loadDEK()) {
        LOG_ERROR("EncryptedStorage: DEK load failed — wrong passphrase?");
        kekDerived = false;
        onFailure();
        return false;
    }

    // Passphrase correct — clear the reserved attempt by writing an
    // attempts=0 sentinel (NOT removing the file; see clearBackoff note).
    clearBackoff();

    // Create fresh unlock token (validUntilEpoch is an absolute Unix timestamp from the client; 0 = no limit)
    if (!writeUnlockToken(bootsRemaining, validUntilEpoch, sessionMaxSeconds)) {
        LOG_WARN("EncryptedStorage: Token write failed after unlock (continuing unlocked this boot)");
    }

    dekLoaded = true;
    s_bootsRemaining = bootsRemaining;
    s_validUntilEpoch = validUntilEpoch;
    setSession(sessionMaxSeconds);
    LOG_INFO("EncryptedStorage: Unlocked with passphrase");
    return true;
}

void lockNow()
{
#ifdef FSCom
    {
        concurrency::LockGuard g(spiLock);
        FSCom.remove(TOKEN_FILENAME);
    }
#endif
    secureWipeKeys();
    s_sessionMaxMs = 0;
    s_sessionStartedMs = 0;
    LOG_INFO("EncryptedStorage: Device locked — token deleted, DEK and KEK material zeroed");
}

void secureWipeKeys()
{
    // M11 (audit): callable from fault handlers. Do NOT take spiLock and do
    // NOT log — interrupt-context safety. Just zero every byte of key
    // material that the rest of the module might leave in BSS.
    meshtastic_security::secure_zero(dek, sizeof(dek));
    dekLoaded = false;
    meshtastic_security::secure_zero(kek, sizeof(kek));
    kekDerived = false;
    meshtastic_security::secure_zero(ephemeralKek, sizeof(ephemeralKek));
    ephemeralKekDerived = false;
}

bool isProvisioned()
{
#ifdef FSCom
    concurrency::LockGuard g(spiLock);
    return FSCom.exists(DEK_FILENAME);
#else
    return false;
#endif
}

bool isUnlocked()
{
    return dekLoaded;
}

bool isLockdownActive()
{
    // Lockdown is "active" iff the device has been provisioned with a
    // passphrase — i.e. a DEK exists on flash. A lockdown-capable build
    // (MESHTASTIC_LOCKDOWN) that has never been provisioned, or that has
    // been disabled via disableLockdown(), is NOT active and behaves like
    // stock firmware: plaintext storage, no redaction, normal logging.
    //
    // Backed by isProvisioned() rather than a separate cached flag so there
    // is a single source of truth (the .dek file) and no chicken-and-egg
    // with reading a config bit out of the encrypted config. The filesystem
    // existence check is cheap (LittleFS stat).
    return isProvisioned();
}

// ---------------------------------------------------------------------------
// Encrypted file I/O
// ---------------------------------------------------------------------------

bool isEncrypted(const char *filename)
{
#ifdef FSCom
    concurrency::LockGuard g(spiLock);
    auto f = FSCom.open(filename, FILE_O_READ);
    if (!f)
        return false;

    uint32_t magic = 0;
    size_t bytesRead = f.read((uint8_t *)&magic, 4);
    f.close();

    return (bytesRead == 4 && magic == MAGIC);
#else
    return false;
#endif
}

bool readAndDecrypt(const char *filename, uint8_t *outBuf, size_t outBufSize, size_t &outLen)
{
    outLen = 0; // MED-6: initialise before any early return so callers never see stale length

    if (!dekLoaded) {
        LOG_ERROR("EncryptedStorage: Not unlocked");
        return false;
    }

    // MED-1: snapshot DEK before any lock acquisition so a concurrent lockNow() cannot
    // zero dek[] mid-operation. If lockNow() races and zeros dek[] before we copy, the
    // snapshot will be zero and the HMAC will fail — a secure failure mode.
    uint8_t dekSnapshot[AES_KEY_SIZE];
    memcpy(dekSnapshot, dek, AES_KEY_SIZE);

#ifdef FSCom
    meshtastic_security::ZeroizingArrayPtr fileBuf{nullptr, meshtastic_security::ZeroizingArrayDeleter{0}};
    size_t fileSize = 0;

    // MED-3: hold spiLock only for file I/O; release it before any crypto operation.
    // spiLock is a non-recursive binary semaphore — re-entry from the same task deadlocks.
    {
        concurrency::LockGuard g(spiLock);
        auto f = FSCom.open(filename, FILE_O_READ);
        if (!f) {
            LOG_ERROR("EncryptedStorage: Can't open %s", filename);
            meshtastic_security::secure_zero(dekSnapshot, sizeof(dekSnapshot));
            return false;
        }

        fileSize = f.size();
        if (fileSize < OVERHEAD) {
            LOG_ERROR("EncryptedStorage: File %s too small (%d bytes)", filename, fileSize);
            f.close();
            meshtastic_security::secure_zero(dekSnapshot, sizeof(dekSnapshot));
            return false;
        }

        // L-1: upper-bound check to prevent OOM / integer overflow on corrupt or oversized files.
        // Derived from the caller's outBufSize: the ciphertext we accept here
        // can never decode to more than outBufSize bytes of plaintext, plus
        // OVERHEAD (nonce + HMAC framing). Anything larger is either corrupt
        // or maliciously oversized. Avoids a hardcoded 64 KB cap that would
        // wrongly reject legitimate large NodeDB files on variants where
        // MAX_NUM_NODES pushes the serialised protobuf past that limit.
        const size_t maxAcceptedFileSize = outBufSize + OVERHEAD;
        if (fileSize > maxAcceptedFileSize) {
            LOG_ERROR("EncryptedStorage: File %s too large (%d bytes, max %d), refusing", filename, fileSize,
                      maxAcceptedFileSize);
            f.close();
            meshtastic_security::secure_zero(dekSnapshot, sizeof(dekSnapshot));
            return false;
        }

        fileBuf = meshtastic_security::make_zeroizing_array(fileSize);
        if (!fileBuf) {
            LOG_ERROR("EncryptedStorage: OOM reading %s", filename);
            f.close();
            meshtastic_security::secure_zero(dekSnapshot, sizeof(dekSnapshot));
            return false;
        }

        size_t bytesRead = f.read(fileBuf.get(), fileSize);
        f.close();

        if (bytesRead != fileSize) {
            LOG_ERROR("EncryptedStorage: Short read on %s", filename);
            meshtastic_security::secure_zero(dekSnapshot, sizeof(dekSnapshot));
            return false;
        }
    } // spiLock released here — MED-3

    // Parse header (outside spiLock)
    size_t pos = 0;
    uint32_t magic;
    memcpy(&magic, fileBuf.get() + pos, 4);
    pos += 4;
    if (magic != MAGIC) {
        LOG_ERROR("EncryptedStorage: Bad magic in %s", filename);
        meshtastic_security::secure_zero(dekSnapshot, sizeof(dekSnapshot));
        return false;
    }

    uint8_t nonce[NONCE_SIZE];
    memcpy(nonce, fileBuf.get() + pos, NONCE_SIZE);
    pos += NONCE_SIZE;

    uint32_t plaintextLen;
    memcpy(&plaintextLen, fileBuf.get() + pos, 4);
    pos += 4;

    size_t ciphertextLen = fileSize - HEADER_SIZE - HMAC_SIZE;
    const uint8_t *ciphertext = fileBuf.get() + pos;
    const uint8_t *storedHmac = fileBuf.get() + fileSize - HMAC_SIZE;

    // M2 (audit): HMAC now covers the full on-disk header — magic +
    // plaintext_len in addition to the nonce + ciphertext that the original
    // design covered. Without this, the 4-byte magic and 4-byte plaintext_len
    // bytes are integrity-protected only by the equality check
    // `plaintextLen == ciphertextLen`, which silently breaks the moment we
    // ever add padding, compression, or AAD to the format. Putting the
    // header inside the MAC closes that pre-condition cleanly.
    //
    // HMAC = HMAC-SHA256(dekSnapshot, magic || nonce || plaintext_len || ciphertext)
    //
    // Format-breaking vs. pre-v1-cleanup files; this is acceptable because
    // we haven't shipped a production lockdown release yet.
    size_t hmacDataLen = 4 /*magic*/ + NONCE_SIZE + 4 /*plaintext_len*/ + ciphertextLen;
    auto hmacData = meshtastic_security::make_zeroizing_array(hmacDataLen);
    if (!hmacData) {
        LOG_ERROR("EncryptedStorage: OOM for HMAC data");
        meshtastic_security::secure_zero(dekSnapshot, sizeof(dekSnapshot));
        return false;
    }
    memcpy(hmacData.get(), &magic, 4);
    memcpy(hmacData.get() + 4, nonce, NONCE_SIZE);
    memcpy(hmacData.get() + 4 + NONCE_SIZE, &plaintextLen, 4);
    memcpy(hmacData.get() + 4 + NONCE_SIZE + 4, ciphertext, ciphertextLen);

    uint8_t computedHmac[HMAC_SIZE];
    nRFCrypto.begin();
    bool hmacOk = computeHMAC(dekSnapshot, AES_KEY_SIZE, hmacData.get(), hmacDataLen, computedHmac);
    nRFCrypto.end();
    hmacData.reset();

    if (!hmacOk || !constTimeEq(computedHmac, storedHmac, HMAC_SIZE)) {
        LOG_ERROR("EncryptedStorage: HMAC verification failed for %s", filename);
        meshtastic_security::secure_zero(computedHmac, sizeof(computedHmac));
        meshtastic_security::secure_zero(dekSnapshot, sizeof(dekSnapshot));
        return false;
    }
    meshtastic_security::secure_zero(computedHmac, sizeof(computedHmac));

    // plaintextLen is not covered by the HMAC, so validate it against the actual ciphertext
    // length derived from the file size. For AES-CTR the two are always equal in a legitimate
    // file; a mismatch means the header field was tampered independently of the ciphertext.
    if (plaintextLen != ciphertextLen) {
        LOG_ERROR("EncryptedStorage: plaintextLen (%d) != ciphertextLen (%d) in %s — header tampered", plaintextLen,
                  (uint32_t)ciphertextLen, filename);
        meshtastic_security::secure_zero(dekSnapshot, sizeof(dekSnapshot));
        return false;
    }

    // Decrypt using dekSnapshot — MED-1
    if (plaintextLen > outBufSize) {
        LOG_ERROR("EncryptedStorage: Output buffer too small for %s (%d > %d)", filename, plaintextLen, outBufSize);
        meshtastic_security::secure_zero(dekSnapshot, sizeof(dekSnapshot));
        return false;
    }

    nRFCrypto.begin();
    bool decOk = aesCtr128(dekSnapshot, nonce, NONCE_SIZE, ciphertext, ciphertextLen, outBuf);
    nRFCrypto.end();
    fileBuf.reset();
    meshtastic_security::secure_zero(dekSnapshot, sizeof(dekSnapshot));

    if (!decOk) {
        LOG_ERROR("EncryptedStorage: Decrypt failed for %s", filename);
        memset(outBuf, 0, ciphertextLen); // MED-7: clear any partial plaintext written to caller's buffer
        return false;
    }

    outLen = plaintextLen;
    LOG_INFO("EncryptedStorage: Decrypted %s (%d bytes)", filename, outLen);
    return true;
#else
    meshtastic_security::secure_zero(dekSnapshot, sizeof(dekSnapshot));
    return false;
#endif
}

bool encryptAndWrite(const char *filename, const uint8_t *plaintext, size_t plaintextLen, bool fullAtomic)
{
    if (!dekLoaded) {
        LOG_ERROR("EncryptedStorage: Not unlocked");
        return false;
    }

    // MED-1: snapshot DEK so a concurrent lockNow() cannot zero dek[] mid-operation.
    uint8_t dekSnapshot[AES_KEY_SIZE];
    memcpy(dekSnapshot, dek, AES_KEY_SIZE);

#ifdef FSCom
    uint8_t nonce[NONCE_SIZE];
    nRFCrypto.begin();
    if (!nRFCrypto.Random.generate(nonce, NONCE_SIZE)) {
        LOG_ERROR("EncryptedStorage: TRNG failed for file nonce");
        nRFCrypto.end();
        meshtastic_security::secure_zero(dekSnapshot, sizeof(dekSnapshot));
        return false;
    }

    size_t ciphertextLen = plaintextLen;
    auto ciphertext = meshtastic_security::make_zeroizing_array(ciphertextLen > 0 ? ciphertextLen : 1);
    if (!ciphertext) {
        LOG_ERROR("EncryptedStorage: OOM for ciphertext");
        nRFCrypto.end();
        meshtastic_security::secure_zero(dekSnapshot, sizeof(dekSnapshot));
        return false;
    }

    bool encOk = aesCtr128(dekSnapshot, nonce, NONCE_SIZE, plaintext, plaintextLen, ciphertext.get());
    if (!encOk) {
        LOG_ERROR("EncryptedStorage: Encrypt failed for %s", filename);
        nRFCrypto.end();
        meshtastic_security::secure_zero(dekSnapshot, sizeof(dekSnapshot));
        return false;
    }

    // M2 (audit): HMAC covers the full header (magic + plaintext_len) in
    // addition to nonce + ciphertext. See readAndDecrypt for the rationale.
    // Must match the read side exactly — keep both updates in lockstep.
    uint32_t magicForMac = MAGIC;
    uint32_t plaintextLenForMac = (uint32_t)plaintextLen;
    size_t hmacDataLen = 4 + NONCE_SIZE + 4 + ciphertextLen;
    auto hmacData = meshtastic_security::make_zeroizing_array(hmacDataLen);
    if (!hmacData) {
        LOG_ERROR("EncryptedStorage: OOM for HMAC data");
        nRFCrypto.end();
        meshtastic_security::secure_zero(dekSnapshot, sizeof(dekSnapshot));
        return false;
    }
    memcpy(hmacData.get(), &magicForMac, 4);
    memcpy(hmacData.get() + 4, nonce, NONCE_SIZE);
    memcpy(hmacData.get() + 4 + NONCE_SIZE, &plaintextLenForMac, 4);
    if (ciphertextLen > 0)
        memcpy(hmacData.get() + 4 + NONCE_SIZE + 4, ciphertext.get(), ciphertextLen);

    uint8_t hmac[HMAC_SIZE];
    bool hmacOk = computeHMAC(dekSnapshot, AES_KEY_SIZE, hmacData.get(), hmacDataLen, hmac);
    nRFCrypto.end();
    hmacData.reset();

    if (!hmacOk) {
        LOG_ERROR("EncryptedStorage: HMAC computation failed for %s", filename);
        meshtastic_security::secure_zero(dekSnapshot, sizeof(dekSnapshot));
        return false;
    }

    meshtastic_security::secure_zero(dekSnapshot, sizeof(dekSnapshot)); // MED-1: no longer needed after HMAC computed

    // SafeFile handles remove-before-write (nRF52) and tmp+readback+rename (other platforms).
    // fullAtomic controls whether the old file is kept until the rename succeeds.
    SafeFile sf(filename, fullAtomic);

    uint32_t magic = MAGIC;
    sf.write((uint8_t *)&magic, 4);
    sf.write(nonce, NONCE_SIZE);
    uint32_t ptLen = (uint32_t)plaintextLen;
    sf.write((uint8_t *)&ptLen, 4);
    sf.write(ciphertext.get(), ciphertextLen);
    sf.write(hmac, HMAC_SIZE);
    ciphertext.reset();

    if (!sf.close()) {
        LOG_ERROR("EncryptedStorage: Write/verify failed for %s", filename);
        return false;
    }

    LOG_INFO("EncryptedStorage: Encrypted %s (%d bytes plaintext)", filename, plaintextLen);
    return true;
#else
    meshtastic_security::secure_zero(dekSnapshot, sizeof(dekSnapshot));
    return false;
#endif
}

bool migrateFile(const char *filename)
{
    // L-2: Precondition — spiLock must NOT be held by the calling task when this function
    // is called. Both isEncrypted() and encryptAndWrite() (called internally) acquire
    // spiLock; since it is a non-recursive binary semaphore, re-entry deadlocks the task.
#ifdef FSCom
    if (isEncrypted(filename)) {
        LOG_DEBUG("EncryptedStorage: %s already encrypted, skip migration", filename);
        return true;
    }

    meshtastic_security::ZeroizingArrayPtr plaintext{nullptr, meshtastic_security::ZeroizingArrayDeleter{0}};
    size_t fileSize = 0;
    {
        concurrency::LockGuard g(spiLock);
        auto f = FSCom.open(filename, FILE_O_READ);
        if (!f) {
            LOG_WARN("EncryptedStorage: %s doesn't exist, skip migration", filename);
            return false;
        }

        fileSize = f.size();
        if (fileSize == 0) {
            f.close();
            return false;
        }

        // M25 (audit): refuse to allocate a buffer for an attacker-injected
        // oversized file. The legitimate ceiling is the largest proto file
        // we ever write — comfortably under 64 KiB on every supported
        // variant. Anything significantly larger is either corrupt or
        // hostile (e.g. DFU file inject); reading it into RAM would OOM
        // the device.
        constexpr size_t kMigrateMaxFileSize = 64 * 1024;
        if (fileSize > kMigrateMaxFileSize) {
            LOG_ERROR("EncryptedStorage: refusing to migrate %s — size %u exceeds %u-byte cap", filename, (unsigned)fileSize,
                      (unsigned)kMigrateMaxFileSize);
            f.close();
            return false;
        }

        plaintext = meshtastic_security::make_zeroizing_array(fileSize);
        if (!plaintext) {
            LOG_ERROR("EncryptedStorage: OOM migrating %s", filename);
            f.close();
            return false;
        }

        size_t bytesRead = f.read(plaintext.get(), fileSize);
        f.close();

        if (bytesRead != fileSize) {
            LOG_ERROR("EncryptedStorage: Short read migrating %s", filename);
            return false;
        }
    }

    bool ok = encryptAndWrite(filename, plaintext.get(), fileSize);

    if (ok) {
        LOG_INFO("EncryptedStorage: Migrated %s to encrypted format", filename);
    }
    return ok;
#else
    return false;
#endif
}

bool migrateFileToPlaintext(const char *filename)
{
    // Inverse of migrateFile: decrypt an encrypted file and rewrite it as
    // plaintext, atomically. Idempotent — a file that is already plaintext
    // is a no-op success, which is what makes the disable flow re-runnable
    // after a power-loss crash.
#ifdef FSCom
    if (!isEncrypted(filename)) {
        LOG_DEBUG("EncryptedStorage: %s already plaintext, skip revert", filename);
        return true;
    }
    if (!dekLoaded) {
        LOG_ERROR("EncryptedStorage: cannot revert %s — not unlocked", filename);
        return false;
    }

    // Determine the plaintext size so we can size the output buffer. The
    // ciphertext length == plaintext length for AES-CTR, so file size minus
    // OVERHEAD is the upper bound. Cap as in migrateFile (M25).
    size_t fileSize = 0;
    {
        concurrency::LockGuard g(spiLock);
        auto f = FSCom.open(filename, FILE_O_READ);
        if (!f) {
            LOG_WARN("EncryptedStorage: %s missing during revert", filename);
            return false;
        }
        fileSize = f.size();
        f.close();
    }
    if (fileSize < OVERHEAD || fileSize > 64 * 1024) {
        LOG_ERROR("EncryptedStorage: %s bad size %u for revert", filename, (unsigned)fileSize);
        return false;
    }
    size_t plaintextCap = fileSize - OVERHEAD;

    auto plaintext = meshtastic_security::make_zeroizing_array(plaintextCap > 0 ? plaintextCap : 1);
    if (!plaintext) {
        LOG_ERROR("EncryptedStorage: OOM reverting %s", filename);
        return false;
    }
    size_t plaintextLen = 0;
    if (!readAndDecrypt(filename, plaintext.get(), plaintextCap, plaintextLen)) {
        LOG_ERROR("EncryptedStorage: decrypt failed reverting %s", filename);
        return false;
    }

    // Write the plaintext over the encrypted file. SafeFile (non-encrypted
    // direct write — NOT encryptAndWrite) atomically replaces it.
    SafeFile sf(filename, /*fullAtomic=*/true);
    sf.write(plaintext.get(), plaintextLen);
    if (!sf.close()) {
        LOG_ERROR("EncryptedStorage: plaintext write failed for %s", filename);
        return false;
    }
    LOG_INFO("EncryptedStorage: Reverted %s to plaintext (%u bytes)", filename, (unsigned)plaintextLen);
    return true;
#else
    return false;
#endif
}

void removeLockdownArtifacts()
{
#ifdef FSCom
    {
        concurrency::LockGuard g(spiLock);
        FSCom.remove(DEK_FILENAME); // deleting this is the commit point: lockdown is now off
        FSCom.remove(TOKEN_FILENAME);
        FSCom.remove(MONO_FILENAME);
        FSCom.remove(BACKOFF_FILENAME);
    }
#endif
    secureWipeKeys();
    s_sessionMaxMs = 0;
    s_sessionStartedMs = 0;
    LOG_INFO("EncryptedStorage: lockdown artifacts removed — device is no longer in lockdown");
}

} // namespace EncryptedStorage

#else
#error "MESHTASTIC_ENCRYPTED_STORAGE requires ARCH_NRF52 (CC310 hardware crypto)."
#endif // ARCH_NRF52

#endif // MESHTASTIC_ENCRYPTED_STORAGE
