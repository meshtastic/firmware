#include "configuration.h"

#ifdef MESHTASTIC_ENCRYPTED_STORAGE

// Common includes — available for all platform implementations
#include "EncryptedStorage.h"
#include "FSCommon.h"
#include "RTC.h"
#include "SPILock.h"
#include "SafeFile.h"

#ifdef ARCH_NRF52

// nRF52 CC310 hardware crypto
#include <Adafruit_nRFCrypto.h>
#include <nrf.h>

extern "C" {
#include "nrf_cc310/include/crys_hmac.h"
#include "nrf_cc310/include/ssi_aes.h"
#include "nrf_cc310/include/ssi_aes_defs.h"
}

namespace EncryptedStorage {

// ---------------------------------------------------------------------------
// File paths and domain-separation strings
// ---------------------------------------------------------------------------

static const char *DEK_FILENAME = "/prefs/.dek";
static const char *TOKEN_FILENAME = "/prefs/.unlock_token";
static const char *BACKOFF_FILENAME = "/prefs/.backoff";

// v1 (legacy): FICR-only KEK, no passphrase
static const char *KEK_DOMAIN_V1 = "meshtastic-tak-kek-v1";
// v2: passphrase-mixed KEK
static const char *KEK_DOMAIN_V2 = "meshtastic-tak-kek-v2";
// Ephemeral: FICR-only, used only to wrap DEK inside the unlock token
static const char *EPHEMERAL_KEK_DOMAIN = "meshtastic-tak-ephemeral-v1";
// HMAC auth label for DEK v2 file
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

// Backoff state — seconds remaining before next passphrase attempt is allowed
static uint32_t s_backoffSecondsRemaining = 0;

// ---------------------------------------------------------------------------
// Passphrase attempt backoff helpers
// ---------------------------------------------------------------------------

// Returns the delay in seconds for a given number of failed attempts.
// Schedule: 5, 10, 20, 40, 80, 160, 320, 900 (capped)
static uint32_t backoffDelay(uint8_t attempts)
{
    if (attempts == 0) return 0;
    uint32_t delay = 5u;
    for (uint8_t i = 1; i < attempts; i++) {
        delay *= 2;
        if (delay >= 900) return 900;
    }
    return delay;
}

// Read attempt count and last-fail epoch from /prefs/.backoff (5 bytes).
// Returns silently on missing or corrupt file (treated as zero attempts).
static void readBackoff(uint8_t &attempts, uint32_t &lastFail)
{
    attempts = 0;
    lastFail = 0;
#ifdef FSCom
    concurrency::LockGuard g(spiLock);
    auto f = FSCom.open(BACKOFF_FILENAME, FILE_O_READ);
    if (!f || f.size() < 5) {
        if (f) f.close();
        return;
    }
    attempts = f.read();
    uint8_t buf[4] = {0, 0, 0, 0};
    size_t n = f.read(buf, 4);
    f.close();
    if (n != 4) {
        attempts = 0; // Corrupt backoff file — treat as no backoff
        return;
    }
    memcpy(&lastFail, buf, 4);
#endif
}

// Write attempt count and last-fail epoch to /prefs/.backoff.
static void writeBackoff(uint8_t attempts, uint32_t lastFail)
{
#ifdef FSCom
    concurrency::LockGuard g(spiLock);
    FSCom.remove(BACKOFF_FILENAME);
    auto f = FSCom.open(BACKOFF_FILENAME, FILE_O_WRITE);
    if (!f) return;
    f.write(attempts);
    uint8_t buf[4];
    memcpy(buf, &lastFail, 4);
    f.write(buf, 4);
    f.close();
#endif
}

// Delete the backoff file and clear in-RAM state on successful unlock.
static void clearBackoff()
{
#ifdef FSCom
    concurrency::LockGuard g(spiLock);
    FSCom.remove(BACKOFF_FILENAME);
#endif
    s_backoffSecondsRemaining = 0;
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
static bool aesCtr128(const uint8_t *key, const uint8_t *nonce, size_t nonceLen,
                      const uint8_t *input, size_t inputLen, uint8_t *output)
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
    err = SaSi_AesFinish(&ctx, remaining, (uint8_t *)input + processed, remaining, output + processed,
                         &finishOutSize);
    if (err != 0) {
        LOG_ERROR("EncryptedStorage: AES finish failed: 0x%x", err);
        SaSi_AesFree(&ctx);
        return false;
    }

    SaSi_AesFree(&ctx);
    return true;
}

/// Compute HMAC-SHA256(key, data). Caller holds CC310.
static bool computeHMAC(const uint8_t *key, size_t keyLen, const uint8_t *data, size_t dataLen,
                        uint8_t *hmacOut)
{
    CRYS_HASH_Result_t hmacResult;
    CRYSError_t err =
        CRYS_HMAC(CRYS_HASH_SHA256_mode, (uint8_t *)key, (uint16_t)keyLen, (uint8_t *)data, dataLen, hmacResult);
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
 * Derive the passphrase-mixed KEK (v2) and store in module-level kek[].
 * SHA-256("device-efuse-data" || FICR_16 || passphrase || KEK_DOMAIN_V2) → first 16 bytes.
 * Caller must hold CC310 (nRFCrypto.begin()).
 */
static bool deriveKEKv2(const uint8_t *passphrase, size_t passphraseLen)
{
    uint8_t efuseData[16];
    readFICR(efuseData);

    static const char *prefix = "device-efuse-data";
    uint8_t sha256Result[32];

    nRFCrypto_Hash hash;
    if (!hash.begin(CRYS_HASH_SHA256_mode)) {
        LOG_ERROR("EncryptedStorage: SHA-256 init failed (KEK v2)");
        memset(efuseData, 0, sizeof(efuseData));
        return false;
    }
    hash.update((uint8_t *)prefix, strlen(prefix));
    hash.update(efuseData, sizeof(efuseData));
    hash.update((uint8_t *)passphrase, passphraseLen);
    hash.update((uint8_t *)KEK_DOMAIN_V2, strlen(KEK_DOMAIN_V2));
    hash.end(sha256Result);

    memcpy(kek, sha256Result, AES_KEY_SIZE);
    memset(sha256Result, 0, sizeof(sha256Result));
    memset(efuseData, 0, sizeof(efuseData));

    kekDerived = true;
    return true;
}

/**
 * Derive the legacy FICR-only KEK (v1) into the module-level kek[].
 * Used only for v1 DEK migration; prefer deriveKEKv2() for all new operations.
 * Caller must hold CC310.
 */
static bool deriveKEKv1()
{
    uint8_t efuseData[16];
    readFICR(efuseData);

    static const char *prefix = "device-efuse-data";
    uint8_t sha256Result[32];

    nRFCrypto_Hash hash;
    if (!hash.begin(CRYS_HASH_SHA256_mode)) {
        LOG_ERROR("EncryptedStorage: SHA-256 init failed (KEK v1)");
        memset(efuseData, 0, sizeof(efuseData));
        return false;
    }
    hash.update((uint8_t *)prefix, strlen(prefix));
    hash.update(efuseData, sizeof(efuseData));
    hash.update((uint8_t *)KEK_DOMAIN_V1, strlen(KEK_DOMAIN_V1));
    hash.end(sha256Result);

    memcpy(kek, sha256Result, AES_KEY_SIZE);
    memset(sha256Result, 0, sizeof(sha256Result));
    memset(efuseData, 0, sizeof(efuseData));

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
        memset(efuseData, 0, sizeof(efuseData));
        return false;
    }
    hash.update((uint8_t *)prefix, strlen(prefix));
    hash.update(efuseData, sizeof(efuseData));
    hash.update((uint8_t *)EPHEMERAL_KEK_DOMAIN, strlen(EPHEMERAL_KEK_DOMAIN));
    hash.end(sha256Result);

    memcpy(ephemeralKek, sha256Result, AES_KEY_SIZE);
    memset(sha256Result, 0, sizeof(sha256Result));
    memset(efuseData, 0, sizeof(efuseData));

    ephemeralKekDerived = true;
    return true;
}

// ---------------------------------------------------------------------------
// Internal helpers: DEK file I/O
// ---------------------------------------------------------------------------

/**
 * Load the DEK from a legacy v1 DEK file using the given KEK (must be in kek[]).
 * Format: [13B nonce][16B AES-CTR(KEK, nonce, DEK)] — 29 bytes, no HMAC.
 * Returns true on read success (cannot verify correctness — no HMAC in v1 format).
 */
static bool loadDEKv1()
{
#ifdef FSCom
    concurrency::LockGuard g(spiLock);
    auto f = FSCom.open(DEK_FILENAME, FILE_O_READ);
    if (!f)
        return false;

    size_t fileSize = f.size();
    if (fileSize != DEK_V1_SIZE) {
        f.close();
        return false;
    }

    uint8_t buf[DEK_V1_SIZE];
    size_t bytesRead = f.read(buf, sizeof(buf));
    f.close();

    if (bytesRead != DEK_V1_SIZE) {
        LOG_ERROR("EncryptedStorage: v1 DEK short read");
        return false;
    }

    nRFCrypto.begin();
    bool decOk = aesCtr128(kek, buf, NONCE_SIZE, buf + NONCE_SIZE, AES_KEY_SIZE, dek);
    nRFCrypto.end();

    if (!decOk) {
        LOG_ERROR("EncryptedStorage: v1 DEK decrypt failed");
        memset(dek, 0, sizeof(dek));
        memset(buf, 0, sizeof(buf));
        return false;
    }

    memset(buf, 0, sizeof(buf));
    LOG_INFO("EncryptedStorage: v1 DEK loaded (migration candidate)");
    return true;
#else
    return false;
#endif
}

/**
 * Load DEK from v2 file using the current kek[].
 * Verifies HMAC before returning the DEK.
 */
static bool loadDEKv2()
{
#ifdef FSCom
    concurrency::LockGuard g(spiLock);
    auto f = FSCom.open(DEK_FILENAME, FILE_O_READ);
    if (!f)
        return false;

    size_t fileSize = f.size();
    if (fileSize != DEK_V2_SIZE) {
        f.close();
        return false;
    }

    uint8_t buf[DEK_V2_SIZE];
    size_t bytesRead = f.read(buf, sizeof(buf));
    f.close();

    if (bytesRead != DEK_V2_SIZE) {
        LOG_ERROR("EncryptedStorage: v2 DEK short read");
        return false;
    }

    // Check magic and version
    uint32_t magic;
    memcpy(&magic, buf, 4);
    if (magic != DEK_MAGIC_V2 || buf[4] != DEK_VERSION_V2) {
        LOG_ERROR("EncryptedStorage: v2 DEK bad magic/version");
        return false;
    }

    uint8_t *nonce = buf + 5;
    uint8_t *encDek = buf + 5 + NONCE_SIZE;

    // Verify HMAC-SHA256(KEK, DEK_AUTH_LABEL || nonce || encDEK)
    size_t authLabelLen = strlen(DEK_AUTH_LABEL);
    size_t hmacInputLen = authLabelLen + NONCE_SIZE + AES_KEY_SIZE;
    uint8_t *hmacInput = new uint8_t[hmacInputLen];
    if (!hmacInput) {
        LOG_ERROR("EncryptedStorage: OOM for DEK HMAC verify");
        return false;
    }
    memcpy(hmacInput, DEK_AUTH_LABEL, authLabelLen);
    memcpy(hmacInput + authLabelLen, nonce, NONCE_SIZE);
    memcpy(hmacInput + authLabelLen + NONCE_SIZE, encDek, AES_KEY_SIZE);

    uint8_t expectedHmac[HMAC_SIZE];
    nRFCrypto.begin();
    bool hmacOk = computeHMAC(kek, AES_KEY_SIZE, hmacInput, hmacInputLen, expectedHmac);
    nRFCrypto.end();
    memset(hmacInput, 0, hmacInputLen);
    delete[] hmacInput;

    if (!hmacOk) {
        return false;
    }

    const uint8_t *storedHmac = buf + DEK_V2_SIZE - HMAC_SIZE;
    if (!constTimeEq(expectedHmac, storedHmac, HMAC_SIZE)) {
        LOG_ERROR("EncryptedStorage: v2 DEK HMAC mismatch — wrong passphrase or tampered file");
        memset(expectedHmac, 0, sizeof(expectedHmac));
        return false;
    }

    // Decrypt DEK into a local candidate — only write to global dek[] on success so that
    // a wrong passphrase attempt does not destroy the live DEK in RAM.
    uint8_t dekCandidate[AES_KEY_SIZE];
    nRFCrypto.begin();
    bool decOk = aesCtr128(kek, nonce, NONCE_SIZE, encDek, AES_KEY_SIZE, dekCandidate);
    nRFCrypto.end();

    memset(expectedHmac, 0, sizeof(expectedHmac));

    if (!decOk) {
        LOG_ERROR("EncryptedStorage: v2 DEK decrypt failed");
        memset(dekCandidate, 0, sizeof(dekCandidate));
        return false;
    }

    memcpy(dek, dekCandidate, AES_KEY_SIZE);
    memset(dekCandidate, 0, sizeof(dekCandidate));

    LOG_INFO("EncryptedStorage: v2 DEK loaded and verified");
    return true;
#else
    return false;
#endif
}

/**
 * Save the current in-RAM dek[] to disk as a v2 DEK file, wrapped with kek[].
 * Overwrites any existing DEK file.
 */
static bool saveDEKv2()
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
        memset(encDek, 0, sizeof(encDek));
        return false;
    }

    // Compute HMAC-SHA256(KEK, DEK_AUTH_LABEL || nonce || encDEK)
    size_t authLabelLen = strlen(DEK_AUTH_LABEL);
    size_t hmacInputLen = authLabelLen + NONCE_SIZE + AES_KEY_SIZE;
    uint8_t *hmacInput = new uint8_t[hmacInputLen];
    if (!hmacInput) {
        LOG_ERROR("EncryptedStorage: OOM for DEK HMAC");
        nRFCrypto.end();
        return false;
    }
    memcpy(hmacInput, DEK_AUTH_LABEL, authLabelLen);
    memcpy(hmacInput + authLabelLen, nonce, NONCE_SIZE);
    memcpy(hmacInput + authLabelLen + NONCE_SIZE, encDek, AES_KEY_SIZE);

    uint8_t hmac[HMAC_SIZE];
    bool hmacOk = computeHMAC(kek, AES_KEY_SIZE, hmacInput, hmacInputLen, hmac);
    nRFCrypto.end();
    memset(hmacInput, 0, hmacInputLen);
    delete[] hmacInput;

    if (!hmacOk) {
        memset(encDek, 0, sizeof(encDek));
        return false;
    }

    // Write file: magic(4)+version(1)+nonce(13)+encDEK(16)+hmac(32) = 66 bytes
    concurrency::LockGuard g(spiLock);
    FSCom.remove(DEK_FILENAME); // prevent O_APPEND accumulation on nRF52
    auto f = FSCom.open(DEK_FILENAME, FILE_O_WRITE);
    if (!f) {
        LOG_ERROR("EncryptedStorage: Can't write DEK file");
        memset(encDek, 0, sizeof(encDek));
        return false;
    }

    uint32_t magic = DEK_MAGIC_V2;
    uint8_t ver = DEK_VERSION_V2;
    f.write((uint8_t *)&magic, 4);
    f.write(&ver, 1);
    f.write(nonce, NONCE_SIZE);
    f.write(encDek, AES_KEY_SIZE);
    f.write(hmac, HMAC_SIZE);
    f.flush();
    f.close();

    memset(nonce, 0, sizeof(nonce));
    memset(encDek, 0, sizeof(encDek));
    memset(hmac, 0, sizeof(hmac));

    LOG_INFO("EncryptedStorage: DEK saved (v2 format)");
    return true;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Internal helpers: unlock token I/O
// ---------------------------------------------------------------------------

/**
 * Write a new unlock token to TOKEN_FILENAME.
 * Wraps the current in-RAM dek[] with ephemeralKek[].
 * @param bootsRemaining  Number of boots this token grants
 * @param validUntilEpoch Unix timestamp after which token is invalid (0 = no limit)
 */
static bool writeUnlockToken(uint8_t bootsRemaining, uint32_t validUntilEpoch)
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
        memset(encDek, 0, sizeof(encDek));
        return false;
    }

    // Build body for HMAC (everything before the trailing HMAC)
    uint8_t body[TOKEN_BODY_SIZE];
    size_t pos = 0;
    uint32_t magic = TOKEN_MAGIC;
    memcpy(body + pos, &magic, 4);
    pos += 4;
    body[pos++] = TOKEN_VERSION;
    memcpy(body + pos, nonce, NONCE_SIZE);
    pos += NONCE_SIZE;
    memcpy(body + pos, encDek, AES_KEY_SIZE);
    pos += AES_KEY_SIZE;
    body[pos++] = bootsRemaining;
    memcpy(body + pos, &validUntilEpoch, 4);
    pos += 4;

    uint8_t hmac[HMAC_SIZE];
    bool hmacOk = computeHMAC(ephemeralKek, AES_KEY_SIZE, body, TOKEN_BODY_SIZE, hmac);
    nRFCrypto.end();

    memset(encDek, 0, sizeof(encDek));

    if (!hmacOk) {
        memset(body, 0, sizeof(body));
        return false;
    }

    concurrency::LockGuard g(spiLock);
    FSCom.remove(TOKEN_FILENAME); // prevent O_APPEND accumulation on nRF52
    auto f = FSCom.open(TOKEN_FILENAME, FILE_O_WRITE);
    if (!f) {
        LOG_ERROR("EncryptedStorage: Can't write token file");
        memset(body, 0, sizeof(body));
        return false;
    }

    f.write(body, TOKEN_BODY_SIZE);
    f.write(hmac, HMAC_SIZE);
    f.flush();
    f.close();

    memset(body, 0, sizeof(body));
    memset(hmac, 0, sizeof(hmac));

    LOG_INFO("EncryptedStorage: Unlock token written (boots=%d, epoch=%u)", bootsRemaining,
             validUntilEpoch);
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
    // Read the token file
    uint8_t buf[TOKEN_TOTAL_SIZE];
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

        size_t bytesRead = f.read(buf, TOKEN_TOTAL_SIZE);
        f.close();

        if (bytesRead != TOKEN_TOTAL_SIZE) {
            LOG_ERROR("EncryptedStorage: Token short read");
            FSCom.remove(TOKEN_FILENAME);
            return false;
        }
    }

    // Verify magic and version
    uint32_t magic;
    memcpy(&magic, buf, 4);
    if (magic != TOKEN_MAGIC || buf[4] != TOKEN_VERSION) {
        LOG_ERROR("EncryptedStorage: Token bad magic/version, deleting");
        concurrency::LockGuard g(spiLock);
        FSCom.remove(TOKEN_FILENAME);
        lockReason = "token_bad_magic";
        return false;
    }

    // Verify HMAC-SHA256(ephemeralKek, body)
    uint8_t computedHmac[HMAC_SIZE];
    nRFCrypto.begin();
    bool hmacOk = computeHMAC(ephemeralKek, AES_KEY_SIZE, buf, TOKEN_BODY_SIZE, computedHmac);
    nRFCrypto.end();

    if (!hmacOk || !constTimeEq(computedHmac, buf + TOKEN_BODY_SIZE, HMAC_SIZE)) {
        LOG_ERROR("EncryptedStorage: Token HMAC failed — tampered or wrong device, deleting");
        memset(computedHmac, 0, sizeof(computedHmac));
        concurrency::LockGuard g(spiLock);
        FSCom.remove(TOKEN_FILENAME);
        lockReason = "token_hmac_fail";
        return false;
    }
    memset(computedHmac, 0, sizeof(computedHmac));

    // Parse fields from body
    size_t pos = 4 + 1; // skip magic + version
    uint8_t *nonce = buf + pos;
    pos += NONCE_SIZE;
    uint8_t *encDek = buf + pos;
    pos += AES_KEY_SIZE;
    uint8_t bootsRemaining = buf[pos++];
    uint32_t validUntilEpoch;
    memcpy(&validUntilEpoch, buf + pos, 4);

    // Check boot count
    if (bootsRemaining == 0) {
        LOG_WARN("EncryptedStorage: Token boot count exhausted, deleting");
        concurrency::LockGuard g(spiLock);
        FSCom.remove(TOKEN_FILENAME);
        lockReason = "token_boots_zero";
        return false;
    }

    // Check time expiry (only if we have a valid RTC)
    if (validUntilEpoch != 0) {
        uint32_t now = getTime(); // from RTC.h; returns 0 if not set
        if (now != 0 && now > validUntilEpoch) {
            LOG_WARN("EncryptedStorage: Token expired (now=%u, until=%u), deleting", now,
                     validUntilEpoch);
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
        memset(dek, 0, sizeof(dek));
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
        writeUnlockToken(newBoots, validUntilEpoch);
    }

    dekLoaded = true;
    s_bootsRemaining = newBoots;
    s_validUntilEpoch = validUntilEpoch;
    LOG_INFO("EncryptedStorage: Token valid, DEK loaded (%d boots remaining)", newBoots);
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
        memset(dek, 0, sizeof(dek));
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

bool provisionPassphrase(const uint8_t *passphrase, size_t passphraseLen, uint8_t bootsRemaining,
                         uint32_t validUntilEpoch)
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

    // If a v1 DEK file exists, migrate it rather than generating a fresh DEK.
    bool migrated = false;
    if (isProvisioned()) {
        nRFCrypto.begin();
        bool v1ok = deriveKEKv1();
        nRFCrypto.end();
        if (v1ok && loadDEKv1()) {
            migrated = true;
            LOG_INFO("EncryptedStorage: Migrating v1 DEK to v2 under passphrase-mixed KEK");
        } else {
            LOG_WARN("EncryptedStorage: Could not load v1 DEK for migration, generating fresh DEK");
        }
    }

    if (!migrated) {
        if (!generateDEK())
            return false;
    }

    // Derive v2 KEK from passphrase
    nRFCrypto.begin();
    bool kekOk = deriveKEKv2(passphrase, passphraseLen);
    nRFCrypto.end();
    if (!kekOk) {
        memset(dek, 0, sizeof(dek));
        return false;
    }

    // Save DEK in v2 format
    if (!saveDEKv2()) {
        memset(dek, 0, sizeof(dek));
        kekDerived = false;
        return false;
    }

    // Create unlock token (validUntilEpoch is an absolute Unix timestamp from the client; 0 = no limit)
    if (!writeUnlockToken(bootsRemaining, validUntilEpoch)) {
        LOG_WARN("EncryptedStorage: Token write failed after provision (continuing unlocked)");
    }

    dekLoaded = true;
    s_bootsRemaining = bootsRemaining;
    s_validUntilEpoch = validUntilEpoch;
    LOG_INFO("EncryptedStorage: Provisioning complete%s", migrated ? " (v1 migrated)" : "");
    return true;
}

bool unlockWithPassphrase(const uint8_t *passphrase, size_t passphraseLen, uint8_t bootsRemaining,
                          uint32_t validUntilEpoch)
{
    // MED-8: proto private_key field is 32 bytes; cap to match (was incorrectly 64)
    if (passphraseLen == 0 || passphraseLen > 32) {
        LOG_ERROR("EncryptedStorage: Invalid passphrase length %d", passphraseLen);
        return false;
    }

    // Exponential backoff: reject attempt if not enough time has elapsed since last failure.
    {
        uint8_t attempts;
        uint32_t lastFail;
        readBackoff(attempts, lastFail);
        if (attempts > 0) {
            uint32_t delay = backoffDelay(attempts);
            uint32_t now = getTime();
            if (lastFail != 0 && now != 0 && (now - lastFail) < delay) {
                s_backoffSecondsRemaining = delay - (now - lastFail);
                LOG_WARN("EncryptedStorage: Passphrase attempt blocked by backoff (%us remaining)", s_backoffSecondsRemaining);
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

    // Derive v2 KEK
    nRFCrypto.begin();
    bool kekOk = deriveKEKv2(passphrase, passphraseLen);
    nRFCrypto.end();
    if (!kekOk)
        return false;

    // Helper: record a failed attempt for backoff tracking.
    auto recordFailure = []() {
        uint8_t attempts;
        uint32_t lastFail;
        readBackoff(attempts, lastFail);
        if (attempts < 255) attempts++;
        writeBackoff(attempts, getTime());
        s_backoffSecondsRemaining = backoffDelay(attempts);
        LOG_WARN("EncryptedStorage: Wrong passphrase (attempt %d, next in %us)", attempts, s_backoffSecondsRemaining);
    };

    // Try loading the v2 DEK (verifies HMAC — wrong passphrase will fail here)
    if (!loadDEKv2()) {
        // Check if this is a v1 DEK file that hasn't been migrated yet.
        // IMPORTANT: check the file size under lock, then release before calling any
        // function that takes spiLock (loadDEKv1, saveDEKv2, recordFailure→readBackoff).
        // spiLock is a binary (non-recursive) semaphore — re-entry deadlocks the task.
#ifdef FSCom
        bool isV1 = false;
        {
            concurrency::LockGuard g(spiLock);
            auto f = FSCom.open(DEK_FILENAME, FILE_O_READ);
            isV1 = f && (f.size() == DEK_V1_SIZE);
            if (f)
                f.close();
        } // spiLock released before any further SPI operations

        if (isV1) {
            LOG_INFO("EncryptedStorage: Detected v1 DEK, migrating");
            nRFCrypto.begin();
            deriveKEKv1();
            nRFCrypto.end();
            if (!loadDEKv1()) {
                LOG_ERROR("EncryptedStorage: v1 DEK load failed during migration");
                memset(dek, 0, sizeof(dek));
                kekDerived = false;
                recordFailure();
                return false;
            }
            // Re-derive v2 KEK (overwritten by deriveKEKv1 above)
            nRFCrypto.begin();
            deriveKEKv2(passphrase, passphraseLen);
            nRFCrypto.end();
            saveDEKv2();
            LOG_INFO("EncryptedStorage: v1 DEK migrated to v2");
        } else {
            LOG_ERROR("EncryptedStorage: v2 DEK load failed — wrong passphrase?");
            kekDerived = false;
            recordFailure();
            return false;
        }
#else
        kekDerived = false;
        recordFailure();
        return false;
#endif
    }

    // Passphrase correct — clear backoff state
    clearBackoff();

    // Create fresh unlock token (validUntilEpoch is an absolute Unix timestamp from the client; 0 = no limit)
    if (!writeUnlockToken(bootsRemaining, validUntilEpoch)) {
        LOG_WARN("EncryptedStorage: Token write failed after unlock (continuing unlocked this boot)");
    }

    dekLoaded = true;
    s_bootsRemaining = bootsRemaining;
    s_validUntilEpoch = validUntilEpoch;
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
    memset(dek, 0, sizeof(dek));
    dekLoaded = false;
    // HIGH-2: zero all derived key material so it is not recoverable from a RAM dump
    memset(kek, 0, sizeof(kek));
    kekDerived = false;
    memset(ephemeralKek, 0, sizeof(ephemeralKek));
    ephemeralKekDerived = false;
    LOG_INFO("EncryptedStorage: Device locked — token deleted, DEK and KEK material zeroed");
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
    uint8_t *fileBuf = nullptr;
    size_t fileSize = 0;

    // MED-3: hold spiLock only for file I/O; release it before any crypto operation.
    // spiLock is a non-recursive binary semaphore — re-entry from the same task deadlocks.
    {
        concurrency::LockGuard g(spiLock);
        auto f = FSCom.open(filename, FILE_O_READ);
        if (!f) {
            LOG_ERROR("EncryptedStorage: Can't open %s", filename);
            memset(dekSnapshot, 0, sizeof(dekSnapshot));
            return false;
        }

        fileSize = f.size();
        if (fileSize < OVERHEAD) {
            LOG_ERROR("EncryptedStorage: File %s too small (%d bytes)", filename, fileSize);
            f.close();
            memset(dekSnapshot, 0, sizeof(dekSnapshot));
            return false;
        }

        // L-1: upper-bound check to prevent OOM / integer overflow on corrupt or oversized files.
        // Meshtastic proto files are well under 64 KB; anything larger is treated as corrupt.
        static constexpr size_t MAX_PROTO_FILE_SIZE = 65536 + OVERHEAD;
        if (fileSize > MAX_PROTO_FILE_SIZE) {
            LOG_ERROR("EncryptedStorage: File %s too large (%d bytes), refusing", filename, fileSize);
            f.close();
            memset(dekSnapshot, 0, sizeof(dekSnapshot));
            return false;
        }

        fileBuf = new uint8_t[fileSize];
        if (!fileBuf) {
            LOG_ERROR("EncryptedStorage: OOM reading %s", filename);
            f.close();
            memset(dekSnapshot, 0, sizeof(dekSnapshot));
            return false;
        }

        size_t bytesRead = f.read(fileBuf, fileSize);
        f.close();

        if (bytesRead != fileSize) {
            LOG_ERROR("EncryptedStorage: Short read on %s", filename);
            memset(fileBuf, 0, fileSize);
            delete[] fileBuf;
            memset(dekSnapshot, 0, sizeof(dekSnapshot));
            return false;
        }
    } // spiLock released here — MED-3

    // Parse header (outside spiLock)
    size_t pos = 0;
    uint32_t magic;
    memcpy(&magic, fileBuf + pos, 4);
    pos += 4;
    if (magic != MAGIC) {
        LOG_ERROR("EncryptedStorage: Bad magic in %s", filename);
        memset(fileBuf, 0, fileSize);
        delete[] fileBuf;
        memset(dekSnapshot, 0, sizeof(dekSnapshot));
        return false;
    }

    uint8_t version = fileBuf[pos++];
    if (version != VERSION) {
        LOG_ERROR("EncryptedStorage: Unsupported version %d in %s", version, filename);
        memset(fileBuf, 0, fileSize);
        delete[] fileBuf;
        memset(dekSnapshot, 0, sizeof(dekSnapshot));
        return false;
    }

    uint8_t nonce[NONCE_SIZE];
    memcpy(nonce, fileBuf + pos, NONCE_SIZE);
    pos += NONCE_SIZE;

    uint32_t plaintextLen;
    memcpy(&plaintextLen, fileBuf + pos, 4);
    pos += 4;

    size_t ciphertextLen = fileSize - HEADER_SIZE - HMAC_SIZE;
    const uint8_t *ciphertext = fileBuf + pos;
    const uint8_t *storedHmac = fileBuf + fileSize - HMAC_SIZE;

    // Verify HMAC-SHA256(dekSnapshot, nonce || ciphertext) — MED-1: use snapshot
    size_t hmacDataLen = NONCE_SIZE + ciphertextLen;
    uint8_t *hmacData = new uint8_t[hmacDataLen];
    if (!hmacData) {
        LOG_ERROR("EncryptedStorage: OOM for HMAC data");
        memset(fileBuf, 0, fileSize); // MED-5: zero before delete on this error path
        delete[] fileBuf;
        memset(dekSnapshot, 0, sizeof(dekSnapshot));
        return false;
    }
    memcpy(hmacData, nonce, NONCE_SIZE);
    memcpy(hmacData + NONCE_SIZE, ciphertext, ciphertextLen);

    uint8_t computedHmac[HMAC_SIZE];
    nRFCrypto.begin();
    bool hmacOk = computeHMAC(dekSnapshot, AES_KEY_SIZE, hmacData, hmacDataLen, computedHmac);
    nRFCrypto.end();
    memset(hmacData, 0, hmacDataLen);
    delete[] hmacData;

    if (!hmacOk || !constTimeEq(computedHmac, storedHmac, HMAC_SIZE)) {
        LOG_ERROR("EncryptedStorage: HMAC verification failed for %s", filename);
        memset(computedHmac, 0, sizeof(computedHmac));
        memset(fileBuf, 0, fileSize);
        delete[] fileBuf;
        memset(dekSnapshot, 0, sizeof(dekSnapshot));
        return false;
    }
    memset(computedHmac, 0, sizeof(computedHmac));

    // plaintextLen is not covered by the HMAC, so validate it against the actual ciphertext
    // length derived from the file size. For AES-CTR the two are always equal in a legitimate
    // file; a mismatch means the header field was tampered independently of the ciphertext.
    if (plaintextLen != ciphertextLen) {
        LOG_ERROR("EncryptedStorage: plaintextLen (%d) != ciphertextLen (%d) in %s — header tampered",
                  plaintextLen, (uint32_t)ciphertextLen, filename);
        memset(fileBuf, 0, fileSize);
        delete[] fileBuf;
        memset(dekSnapshot, 0, sizeof(dekSnapshot));
        return false;
    }

    // Decrypt using dekSnapshot — MED-1
    if (plaintextLen > outBufSize) {
        LOG_ERROR("EncryptedStorage: Output buffer too small for %s (%d > %d)", filename, plaintextLen,
                  outBufSize);
        memset(fileBuf, 0, fileSize);
        delete[] fileBuf;
        memset(dekSnapshot, 0, sizeof(dekSnapshot));
        return false;
    }

    nRFCrypto.begin();
    bool decOk = aesCtr128(dekSnapshot, nonce, NONCE_SIZE, ciphertext, ciphertextLen, outBuf);
    nRFCrypto.end();
    memset(fileBuf, 0, fileSize);
    delete[] fileBuf;
    memset(dekSnapshot, 0, sizeof(dekSnapshot));

    if (!decOk) {
        LOG_ERROR("EncryptedStorage: Decrypt failed for %s", filename);
        memset(outBuf, 0, ciphertextLen); // MED-7: clear any partial plaintext written to caller's buffer
        return false;
    }

    outLen = plaintextLen;
    LOG_INFO("EncryptedStorage: Decrypted %s (%d bytes)", filename, outLen);
    return true;
#else
    memset(dekSnapshot, 0, sizeof(dekSnapshot));
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
        memset(dekSnapshot, 0, sizeof(dekSnapshot));
        return false;
    }

    size_t ciphertextLen = plaintextLen;
    uint8_t *ciphertext = new uint8_t[ciphertextLen > 0 ? ciphertextLen : 1];
    if (!ciphertext) {
        LOG_ERROR("EncryptedStorage: OOM for ciphertext");
        nRFCrypto.end();
        memset(dekSnapshot, 0, sizeof(dekSnapshot));
        return false;
    }

    bool encOk = aesCtr128(dekSnapshot, nonce, NONCE_SIZE, plaintext, plaintextLen, ciphertext);
    if (!encOk) {
        LOG_ERROR("EncryptedStorage: Encrypt failed for %s", filename);
        memset(ciphertext, 0, ciphertextLen);
        delete[] ciphertext;
        nRFCrypto.end();
        memset(dekSnapshot, 0, sizeof(dekSnapshot));
        return false;
    }

    size_t hmacDataLen = NONCE_SIZE + ciphertextLen;
    uint8_t *hmacData = new uint8_t[hmacDataLen > 0 ? hmacDataLen : 1];
    if (!hmacData) {
        LOG_ERROR("EncryptedStorage: OOM for HMAC data");
        memset(ciphertext, 0, ciphertextLen);
        delete[] ciphertext;
        nRFCrypto.end();
        memset(dekSnapshot, 0, sizeof(dekSnapshot));
        return false;
    }
    memcpy(hmacData, nonce, NONCE_SIZE);
    if (ciphertextLen > 0)
        memcpy(hmacData + NONCE_SIZE, ciphertext, ciphertextLen);

    uint8_t hmac[HMAC_SIZE];
    bool hmacOk = computeHMAC(dekSnapshot, AES_KEY_SIZE, hmacData, hmacDataLen, hmac);
    nRFCrypto.end();
    memset(hmacData, 0, hmacDataLen);
    delete[] hmacData;

    if (!hmacOk) {
        LOG_ERROR("EncryptedStorage: HMAC computation failed for %s", filename);
        memset(ciphertext, 0, ciphertextLen);
        delete[] ciphertext;
        memset(dekSnapshot, 0, sizeof(dekSnapshot));
        return false;
    }

    memset(dekSnapshot, 0, sizeof(dekSnapshot)); // MED-1: no longer needed after HMAC computed

    // SafeFile handles remove-before-write (nRF52) and tmp+readback+rename (other platforms).
    // fullAtomic controls whether the old file is kept until the rename succeeds.
    SafeFile sf(filename, fullAtomic);

    uint32_t magic = MAGIC;
    sf.write((uint8_t *)&magic, 4);
    uint8_t ver = VERSION;
    sf.write(&ver, 1);
    sf.write(nonce, NONCE_SIZE);
    uint32_t ptLen = (uint32_t)plaintextLen;
    sf.write((uint8_t *)&ptLen, 4);
    sf.write(ciphertext, ciphertextLen);
    sf.write(hmac, HMAC_SIZE);

    memset(ciphertext, 0, ciphertextLen);
    delete[] ciphertext;

    if (!sf.close()) {
        LOG_ERROR("EncryptedStorage: Write/verify failed for %s", filename);
        return false;
    }

    LOG_INFO("EncryptedStorage: Encrypted %s (%d bytes plaintext)", filename, plaintextLen);
    return true;
#else
    memset(dekSnapshot, 0, sizeof(dekSnapshot));
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

    uint8_t *plaintext = nullptr;
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

        plaintext = new uint8_t[fileSize];
        if (!plaintext) {
            LOG_ERROR("EncryptedStorage: OOM migrating %s", filename);
            f.close();
            return false;
        }

        size_t bytesRead = f.read(plaintext, fileSize);
        f.close();

        if (bytesRead != fileSize) {
            LOG_ERROR("EncryptedStorage: Short read migrating %s", filename);
            memset(plaintext, 0, fileSize);
            delete[] plaintext;
            return false;
        }
    }

    bool ok = encryptAndWrite(filename, plaintext, fileSize);
    memset(plaintext, 0, fileSize);
    delete[] plaintext;

    if (ok) {
        LOG_INFO("EncryptedStorage: Migrated %s to encrypted format", filename);
    }
    return ok;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Legacy init() — FICR-only KEK, no passphrase, no token
// ---------------------------------------------------------------------------

bool init()
{
    nRFCrypto.begin();

    if (!deriveKEKv1()) {
        LOG_ERROR("EncryptedStorage: KEK derivation failed");
        nRFCrypto.end();
        return false;
    }

    // Also derive ephemeral KEK so token ops are available if needed
    deriveEphemeralKEK();

    nRFCrypto.end();

    if (!loadDEKv1()) {
        LOG_INFO("EncryptedStorage: No existing DEK, generating new one");
        if (!generateDEK())
            return false;
        // Save in v1 format (legacy path: nonce + AES-CTR(KEK, nonce, DEK))
        uint8_t nonce[NONCE_SIZE];
        nRFCrypto.begin();
        bool ok = nRFCrypto.Random.generate(nonce, NONCE_SIZE);
        nRFCrypto.end();
        if (!ok) {
            LOG_ERROR("EncryptedStorage: TRNG failed");
            memset(dek, 0, sizeof(dek));
            return false;
        }
        uint8_t encDek[AES_KEY_SIZE];
        nRFCrypto.begin();
        ok = aesCtr128(kek, nonce, NONCE_SIZE, dek, AES_KEY_SIZE, encDek);
        nRFCrypto.end();
        if (!ok) {
            memset(dek, 0, sizeof(dek));
            return false;
        }
        concurrency::LockGuard g(spiLock);
        auto f = FSCom.open(DEK_FILENAME, FILE_O_WRITE);
        if (!f) {
            memset(dek, 0, sizeof(dek));
            return false;
        }
        f.write(nonce, NONCE_SIZE);
        f.write(encDek, AES_KEY_SIZE);
        f.flush();
        f.close();
        memset(nonce, 0, sizeof(nonce));
        memset(encDek, 0, sizeof(encDek));
    }

    dekLoaded = true;
    LOG_INFO("EncryptedStorage: Initialized (legacy mode)");
    return true;
}

} // namespace EncryptedStorage

#elif defined(ARCH_ESP32)

// ---------------------------------------------------------------------------
// ESP32 stub implementation
//
// Crypto not yet ported (mbedTLS AES-CTR + HMAC-SHA256 TODO).
// Key management functions are no-ops; file I/O is plaintext pass-through.
//
// What still works on ESP32 LOCKDOWN today:
//   - MESHTASTIC_PHONEAPI_ACCESS_CONTROL — channel PSKs, private key, admin
//     keys are redacted from any unauthenticated BLE/USB/TCP client
//   - PKC admin key auth — client sends a PKC-encrypted admin packet matching
//     config.security.admin_key[0..2]; AdminModule calls authorizeLocalAdmin()
//   - HARDENED_DEFAULTS — is_managed=true, RANDOM_PIN, TAK role at first boot
//   - Module exclusions + DEBUG_MUTE — attack-surface reduction
//
// What is not yet available on ESP32:
//   - At-rest encryption of proto files (plaintext on flash)
//   - Passphrase-based unlock / boot-count token
//   - APPROTECT (configure via espsecure.py + eFuse burn at provisioning)
//
// To implement: replace this block with mbedTLS calls and esp_efuse chip ID.
// ---------------------------------------------------------------------------

#include "concurrency/LockGuard.h"

// When mbedTLS crypto is implemented, add:
// #include <mbedtls/aes.h>
// #include <mbedtls/md.h>
// #include <esp_system.h>   // esp_fill_random()
// #include <esp_efuse.h>    // esp_efuse_mac_get_default()

namespace EncryptedStorage {

// Storage level is always "unlocked" on ESP32 (no crypto gate yet).
// Per-connection access control is still enforced by PhoneAPI via PKC admin key.
static bool s_unlocked = true;

void initLocked()
{
    s_unlocked = true;
    LOG_INFO("EncryptedStorage(ESP32): no crypto implemented, plaintext pass-through");
}

// Passphrase management — no-ops until mbedTLS is wired up.
// Return true so AdminModule doesn't report a failure to the client.
bool provisionPassphrase(const uint8_t *, size_t, uint8_t, uint32_t) { return true; }
bool unlockWithPassphrase(const uint8_t *, size_t, uint8_t, uint32_t) { return true; }
void lockNow() {} // can't lock without crypto; PKC revocation handles per-connection auth
bool isProvisioned() { return true; }
bool isUnlocked() { return s_unlocked; }
const char *getLockReason() { return "ok"; }
uint8_t getBootsRemaining() { return 0xFF; }
uint32_t getValidUntilEpoch() { return 0; }
uint32_t getBackoffSecondsRemaining() { return 0; }
bool init() { return true; }

bool isEncrypted(const char *filename)
{
    // We never write MENC-wrapped files on ESP32, but check the magic in case
    // a file was transferred from an nRF52 image (which we cannot decrypt here).
#ifdef FSCom
    concurrency::LockGuard g(spiLock);
    auto f = FSCom.open(filename, FILE_O_READ);
    if (!f || f.size() < 4) {
        if (f)
            f.close();
        return false;
    }
    uint32_t magic = 0;
    f.read((uint8_t *)&magic, 4);
    f.close();
    return magic == MAGIC;
#else
    return false;
#endif
}

bool readAndDecrypt(const char *filename, uint8_t *outBuf, size_t outBufSize, size_t &outLen)
{
    // Plaintext pass-through — no decryption.
#ifdef FSCom
    concurrency::LockGuard g(spiLock);
    auto f = FSCom.open(filename, FILE_O_READ);
    if (!f) {
        LOG_WARN("EncryptedStorage(ESP32): cannot open %s", filename);
        outLen = 0;
        return false;
    }
    size_t fileSize = f.size();
    if (fileSize > outBufSize) {
        LOG_WARN("EncryptedStorage(ESP32): %s too large (%d > %d)", filename, fileSize, outBufSize);
        f.close();
        outLen = 0;
        return false;
    }
    size_t n = f.read(outBuf, fileSize);
    f.close();
    outLen = n;
    return n == fileSize;
#else
    outLen = 0;
    return false;
#endif
}

bool encryptAndWrite(const char *filename, const uint8_t *plaintext, size_t plaintextLen, bool fullAtomic)
{
    // Plaintext pass-through — no encryption.
    SafeFile sf(filename, fullAtomic);
    sf.write(plaintext, plaintextLen);
    if (!sf.close()) {
        LOG_ERROR("EncryptedStorage(ESP32): write failed for %s", filename);
        return false;
    }
    LOG_INFO("EncryptedStorage(ESP32): wrote %s (%d bytes, plaintext)", filename, plaintextLen);
    return true;
}

bool migrateFile(const char *filename)
{
    // No encryption on ESP32 yet — nothing to migrate.
    // If an MENC file is found (e.g. transferred from an nRF52 image), we
    // cannot decrypt it, so leave it alone and let the caller fail gracefully.
    if (isEncrypted(filename)) {
        LOG_WARN("EncryptedStorage(ESP32): MENC file at %s — no crypto, skipping", filename);
        return false;
    }
    return true;
}

} // namespace EncryptedStorage

#else
#error "MESHTASTIC_ENCRYPTED_STORAGE requires ARCH_NRF52 or ARCH_ESP32"
#endif // arch

#endif // MESHTASTIC_ENCRYPTED_STORAGE
