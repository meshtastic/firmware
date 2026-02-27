#pragma once

#ifdef MESHTASTIC_ENCRYPTED_STORAGE

#include <cstddef>
#include <cstdint>

/**
 * Encrypted storage layer for TAK secure builds.
 *
 * Key hierarchy:
 *   FICR eFuse IDs + passphrase -> SHA-256 -> KEK_v2 (16 bytes, never stored)
 *   KEK_v2 wraps -> DEK (Data Encryption Key, 16 bytes, random, stored in /prefs/.dek)
 *   DEK encrypts -> proto files via AES-128-CTR + HMAC-SHA256(KEK)
 *
 *   Ephemeral KEK (FICR-only, no passphrase) -> wraps DEK in the unlock token only.
 *   Unlock token (/prefs/.unlock_token) — valid for N boots and/or M hours after provisioning.
 *
 * Boot flow:
 *   1. initLocked()  — derive ephemeral KEK, try unlock token
 *   2a. Token valid → UNLOCKED (DEK in RAM, all encrypted files accessible)
 *   2b. No token, no DEK file → NOT PROVISIONED (operator must call provisionPassphrase)
 *   2c. No token, DEK file exists → LOCKED (operator must call unlockWithPassphrase)
 *   3. provisionPassphrase() / unlockWithPassphrase() complete the unlock
 *   4. lockNow() immediately invalidates the token and zeroes the DEK from RAM
 *
 * Encrypted proto file format ("MENC"):
 *   [4B]  Magic 0x4D454E43 ("MENC")
 *   [1B]  Version 0x01
 *   [13B] Nonce (random per write)
 *   [4B]  Original plaintext length (LE uint32)
 *   [NB]  AES-128-CTR ciphertext
 *   [32B] HMAC-SHA256(DEK, nonce || ciphertext)
 *   Total overhead: 54 bytes per file.
 *
 * DEK file format v2 ("MDEK"):
 *   [4B]  Magic 0x4D44454B ("MDEK")
 *   [1B]  Version 0x02
 *   [13B] Nonce (random per write)
 *   [16B] AES-128-CTR(KEK_v2, nonce, DEK)
 *   [32B] HMAC-SHA256(KEK_v2, "mdek-auth" || nonce || encrypted_DEK)
 *   Total: 66 bytes.
 *   Legacy v1 DEK file is 29 bytes (no magic, no HMAC) and is migrated on first passphrase set.
 *
 * Unlock token format ("UTOK"):
 *   [4B]  Magic 0x55544F4B ("UTOK")
 *   [1B]  Version 0x01
 *   [13B] Nonce (random per write)
 *   [16B] AES-128-CTR(ephemeralKEK, nonce, DEK)
 *   [1B]  boots_remaining
 *   [4B]  valid_until_epoch (LE uint32, 0 = no time limit)
 *   [32B] HMAC-SHA256(ephemeralKEK, all above fields)
 *   Total: 71 bytes.
 */

namespace EncryptedStorage {

// ---------------------------------------------------------------------------
// File format constants
// ---------------------------------------------------------------------------

static constexpr uint32_t MAGIC = 0x4D454E43;          // "MENC" — encrypted proto files
static constexpr uint8_t VERSION = 0x01;
static constexpr size_t NONCE_SIZE = 13;
static constexpr size_t HMAC_SIZE = 32;
static constexpr size_t HEADER_SIZE = 4 + 1 + NONCE_SIZE + 4; // magic+version+nonce+plaintext_len
static constexpr size_t OVERHEAD = HEADER_SIZE + HMAC_SIZE;    // 54 bytes
static constexpr size_t AES_KEY_SIZE = 16;
static constexpr size_t AES_BLOCK_SIZE = 16;

static constexpr uint32_t DEK_MAGIC_V2 = 0x4D44454B;   // "MDEK"
static constexpr uint8_t DEK_VERSION_V2 = 0x02;
static constexpr size_t DEK_V1_SIZE = NONCE_SIZE + AES_KEY_SIZE;                           // 29 bytes
static constexpr size_t DEK_V2_SIZE = 4 + 1 + NONCE_SIZE + AES_KEY_SIZE + HMAC_SIZE;      // 66 bytes

static constexpr uint32_t TOKEN_MAGIC = 0x55544F4B;     // "UTOK"
static constexpr uint8_t TOKEN_VERSION = 0x01;
// Body = magic(4)+ver(1)+nonce(13)+encDEK(16)+boots(1)+epoch(4) = 39 bytes
static constexpr size_t TOKEN_BODY_SIZE = 4 + 1 + NONCE_SIZE + AES_KEY_SIZE + 1 + 4;
static constexpr size_t TOKEN_TOTAL_SIZE = TOKEN_BODY_SIZE + HMAC_SIZE;                    // 71 bytes

static constexpr uint8_t TOKEN_DEFAULT_BOOTS = 50;

// ---------------------------------------------------------------------------
// Passphrase-gated boot API
// ---------------------------------------------------------------------------

/**
 * Boot-time init: derive ephemeral KEK and attempt to unlock via the stored token.
 * Sets isUnlocked()=true if the token is present and valid.
 * Must be called after fsInit(), before loadFromDisk().
 */
void initLocked();

/**
 * First-time provisioning: set the device passphrase, generate a fresh DEK,
 * save it wrapped with the passphrase-mixed KEK, and create an unlock token.
 *
 * If a legacy v1 DEK file already exists, it is migrated rather than replaced,
 * preserving all previously encrypted config files.
 *
 * @param passphrase      Raw passphrase bytes (need not be NUL-terminated)
 * @param passphraseLen   Length in bytes (1–64)
 * @param bootsRemaining   Token valid for this many boots (default TOKEN_DEFAULT_BOOTS)
 * @param validUntilEpoch  Absolute Unix timestamp after which token expires (0 = no time limit)
 * @return true on success
 */
bool provisionPassphrase(const uint8_t *passphrase, size_t passphraseLen,
                         uint8_t bootsRemaining = TOKEN_DEFAULT_BOOTS, uint32_t validUntilEpoch = 0);

/**
 * Unlock after token expiry (or after lockNow()): derive KEK from passphrase,
 * unwrap the stored DEK, and create a fresh unlock token.
 *
 * @param passphrase       Raw passphrase bytes
 * @param passphraseLen    Length in bytes (1–64)
 * @param bootsRemaining   New token valid for this many boots
 * @param validUntilEpoch  Absolute Unix timestamp after which token expires (0 = no time limit)
 * @return true if passphrase was correct and DEK is now loaded
 */
bool unlockWithPassphrase(const uint8_t *passphrase, size_t passphraseLen,
                          uint8_t bootsRemaining = TOKEN_DEFAULT_BOOTS, uint32_t validUntilEpoch = 0);

/**
 * Immediately lock: delete the unlock token and zero the DEK from RAM.
 * The device will require the passphrase on the next boot (or connection).
 */
void lockNow();

/** Returns true if the DEK file exists (device has been provisioned). */
bool isProvisioned();

/** Returns true if the DEK is loaded in RAM (device is unlocked). */
bool isUnlocked();

/**
 * Returns a short string describing why the device is locked (set during initLocked()).
 * Useful for client-side diagnostics. Examples:
 *   "token_missing"      — no unlock token file found
 *   "token_wrong_size"   — token file exists but is corrupt
 *   "token_bad_magic"    — wrong magic/version bytes
 *   "token_hmac_fail"    — HMAC mismatch (tampered or wrong device)
 *   "token_boots_zero"   — boot count exhausted
 *   "token_expired"      — TTL expired
 *   "token_dek_fail"     — DEK decrypt failed
 *   "not_provisioned"    — no DEK file; needs first provisioning
 *   "ok"                 — unlocked successfully via token
 */
const char *getLockReason();

/** Boots remaining in the current unlock token (0 if not unlocked or last boot consumed). */
uint8_t getBootsRemaining();

/** Unix epoch at which the current unlock token expires (0 = no time limit or not unlocked). */
uint32_t getValidUntilEpoch();

/** Seconds remaining before next passphrase attempt is allowed (0 = can attempt now). */
uint32_t getBackoffSecondsRemaining();

// ---------------------------------------------------------------------------
// Encrypted file I/O (require isUnlocked())
// ---------------------------------------------------------------------------

/** Returns true if the file starts with the MENC magic bytes. */
bool isEncrypted(const char *filename);

/**
 * Read and decrypt a file into outBuf.
 * Returns true on success; sets outLen to the plaintext byte count.
 */
bool readAndDecrypt(const char *filename, uint8_t *outBuf, size_t outBufSize, size_t &outLen);

/**
 * Encrypt plaintext and write to filename.
 * Returns true on success.
 */
bool encryptAndWrite(const char *filename, const uint8_t *plaintext, size_t plaintextLen,
                     bool fullAtomic = false);

/**
 * Migrate a plaintext proto file to encrypted format in-place.
 * Returns true on success or if already encrypted.
 */
bool migrateFile(const char *filename);

// ---------------------------------------------------------------------------
// Legacy init (FICR-only KEK, no passphrase, no token)
// ---------------------------------------------------------------------------

/**
 * Original init(): derives KEK from FICR only, loads or generates DEK.
 * Retained for non-TAK builds and unit tests. Prefer initLocked() for TAK builds.
 */
bool init();

} // namespace EncryptedStorage

#endif // MESHTASTIC_ENCRYPTED_STORAGE
