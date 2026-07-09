#pragma once

#ifdef MESHTASTIC_ENCRYPTED_STORAGE

#include <cstddef>
#include <cstdint>

/**
 * Encrypted storage layer for lockdown builds.
 *
 * Key hierarchy:
 *   FICR eFuse IDs + passphrase -> SHA-256 -> KEK (16 bytes, never stored)
 *   KEK wraps -> DEK (Data Encryption Key, 16 bytes, random, stored in /prefs/.dek)
 *   DEK encrypts -> proto files via AES-128-CTR + HMAC-SHA256(DEK)
 *     (the DEK file itself is HMAC'd with KEK; only proto files use HMAC(DEK))
 *
 *   Ephemeral KEK (FICR-only, no passphrase) -> wraps DEK in the unlock token only.
 *   Unlock token (/prefs/.unlock_token) - valid for N boots and/or M hours after provisioning.
 *
 * Boot flow:
 *   1. initLocked()  - derive ephemeral KEK, try unlock token
 *   2a. Token valid → UNLOCKED (DEK in RAM, all encrypted files accessible)
 *   2b. No token, no DEK file → NOT PROVISIONED (operator must call provisionPassphrase)
 *   2c. No token, DEK file exists → LOCKED (operator must call unlockWithPassphrase)
 *   3. provisionPassphrase() / unlockWithPassphrase() complete the unlock
 *   4. lockNow() immediately invalidates the token and zeroes the DEK from RAM
 *
 * On-disk formats carry a 4-byte magic but no version byte: this layer has
 * never shipped, so there are no older files to stay compatible with. The
 * magic alone identifies each format; a corrupt or foreign file fails the
 * magic check (and, for the keyed formats, the HMAC).
 *
 * Encrypted proto file format ("MENC"):
 *   [4B]  Magic 0x4D454E43 ("MENC")
 *   [13B] Nonce (random per write)
 *   [4B]  Original plaintext length (LE uint32)
 *   [NB]  AES-128-CTR ciphertext
 *   [32B] HMAC-SHA256(DEK, magic || nonce || plaintext_len || ciphertext)
 *   Total overhead: 53 bytes per file.
 *
 * DEK file format ("MDEK"):
 *   [4B]  Magic 0x4D44454B ("MDEK")
 *   [13B] Nonce (random per write)
 *   [16B] AES-128-CTR(KEK, nonce, DEK)
 *   [32B] HMAC-SHA256(KEK, "mdek-auth" || nonce || encrypted_DEK)
 *   Total: 65 bytes.
 *
 * Unlock token format ("UTOK"):
 *   [4B]  Magic 0x55544F4B ("UTOK")
 *   [13B] Nonce (random per write)
 *   [16B] AES-128-CTR(ephemeralKEK, nonce, DEK)
 *   [1B]  boots_remaining
 *   [4B]  valid_until_epoch (LE uint32, 0 = no time limit)
 *   [4B]  session_max_seconds (LE uint32, 0 = no session limit)
 *   [4B]  monotonic_counter (LE uint32) - see /prefs/.tokmono
 *   [32B] HMAC-SHA256(ephemeralKEK, all above fields)
 *   Total: 78 bytes.
 *
 * Monotonic counter file (/prefs/.tokmono):
 *   [4B]  highest counter ever issued (LE uint32)
 *   [32B] HMAC-SHA256(ephemeralKEK, "tokmono-auth" || counter)
 *   Total: 36 bytes.
 *   readAndConsumeToken rejects any token whose body counter is less
 *   than the persisted value, defeating a flash-write-only attacker who
 *   tries to restore an older (e.g. higher-boot-count) token.
 *
 * Backoff state file (/prefs/.backoff):
 *   [1B]  attempts
 *   [1B]  bootsSinceFail
 *   [4B]  lastFailEpoch (LE uint32)
 *   [32B] HMAC-SHA256(ephemeralKEK, "backoff-auth" || body)
 *   Total: 38 bytes. Missing / short / MAC-fail are all treated as
 *   max-attempts so a tamper-delete can only increase the wait.
 */

namespace EncryptedStorage
{

// ---------------------------------------------------------------------------
// File format constants
// ---------------------------------------------------------------------------

static constexpr uint32_t MAGIC = 0x4D454E43; // "MENC" - encrypted proto files
static constexpr size_t NONCE_SIZE = 13;
static constexpr size_t HMAC_SIZE = 32;
static constexpr size_t HEADER_SIZE = 4 + NONCE_SIZE + 4;   // magic+nonce+plaintext_len
static constexpr size_t OVERHEAD = HEADER_SIZE + HMAC_SIZE; // 53 bytes
static constexpr size_t AES_KEY_SIZE = 16;
static constexpr size_t AES_BLOCK_SIZE = 16;

static constexpr uint32_t DEK_MAGIC = 0x4D44454B;                             // "MDEK"
static constexpr size_t DEK_SIZE = 4 + NONCE_SIZE + AES_KEY_SIZE + HMAC_SIZE; // 65 bytes

static constexpr uint32_t TOKEN_MAGIC = 0x55544F4B; // "UTOK"
// magic(4) + nonce(NONCE_SIZE=13) + encDek(AES_KEY_SIZE=16)
//   + bootsRemaining(1) + validUntilEpoch(4) + sessionMaxSeconds(4)
//   + monotonicCounter(4) = 46 bytes
static constexpr size_t TOKEN_BODY_SIZE = 4 + NONCE_SIZE + AES_KEY_SIZE + 1 + 4 + 4 + 4;
static constexpr size_t TOKEN_TOTAL_SIZE = TOKEN_BODY_SIZE + HMAC_SIZE; // 78 bytes

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
 * @param passphrase        Raw passphrase bytes (need not be NUL-terminated)
 * @param passphraseLen     Length in bytes (1-32; matches the proto private_key field size)
 * @param bootsRemaining    Token valid for this many boots (default TOKEN_DEFAULT_BOOTS)
 * @param validUntilEpoch   Absolute Unix timestamp after which token expires (0 = no time limit)
 * @param sessionMaxSeconds Per-boot uptime cap on the unlocked session (0 = no cap).
 *                          Persists in the token; cold-boot via token inherits the same cap.
 * @return true on success
 */
bool provisionPassphrase(const uint8_t *passphrase, size_t passphraseLen, uint8_t bootsRemaining = TOKEN_DEFAULT_BOOTS,
                         uint32_t validUntilEpoch = 0, uint32_t sessionMaxSeconds = 0);

/**
 * Unlock after token expiry (or after lockNow()): derive KEK from passphrase,
 * unwrap the stored DEK, and create a fresh unlock token.
 *
 * @param passphrase        Raw passphrase bytes
 * @param passphraseLen     Length in bytes (1-32; matches the proto private_key field size)
 * @param bootsRemaining    New token valid for this many boots
 * @param validUntilEpoch   Absolute Unix timestamp after which token expires (0 = no time limit)
 * @param sessionMaxSeconds Per-boot uptime cap on the unlocked session (0 = no cap).
 *                          Persists in the new token; reboot starts a fresh session window.
 * @return true if passphrase was correct and DEK is now loaded
 */
bool unlockWithPassphrase(const uint8_t *passphrase, size_t passphraseLen, uint8_t bootsRemaining = TOKEN_DEFAULT_BOOTS,
                          uint32_t validUntilEpoch = 0, uint32_t sessionMaxSeconds = 0);

/**
 * Immediately lock: delete the unlock token and zero the DEK from RAM.
 * The device will require the passphrase on the next boot (or connection).
 */
void lockNow();

/**
 * Wipe in-RAM key material WITHOUT touching flash. Designed to be called
 * from fault / watchdog handlers before any coredump or RAM-snapshot path
 * runs, so the DEK / KEK / ephemeralKEK don't end up in crash reports.
 *
 * Safe to call from interrupt context: does not take any FreeRTOS locks
 * and does not log. Equivalent to the RAM-wipe half of lockNow() with the
 * token file left intact (so the device can still auto-unlock on the
 * next normal boot via the token).
 */
void secureWipeKeys();

/** Returns true if the DEK file exists (device has been provisioned). */
bool isProvisioned();

/** Returns true if the DEK is loaded in RAM (device is unlocked). */
bool isUnlocked();

/**
 * Returns true when lockdown is active on this device (== isProvisioned()).
 * The runtime gate for all access-control / redaction / locked-boot
 * behavior. A lockdown-CAPABLE build that has not been provisioned (or has
 * been disabled) returns false here and runs like stock firmware.
 */
bool isLockdownActive();

/**
 * Decrypt one encrypted file back to plaintext in place (the inverse of
 * migrateFile). Idempotent: a file that is already plaintext returns true
 * without touching it. Requires isUnlocked() (DEK in RAM). Used by the
 * lockdown-disable flow; NodeDB drives the per-file iteration since it owns
 * the proto filenames.
 *
 * @return true on success or if the file was already plaintext.
 */
bool migrateFileToPlaintext(const char *filename);

/**
 * Final step of disabling lockdown: remove the DEK, unlock token,
 * monotonic-counter, and backoff files, then wipe the in-RAM keys.
 * Call this ONLY after every encrypted file has been reverted to plaintext
 * via migrateFileToPlaintext() - deleting the DEK first would make any
 * remaining encrypted file permanently unreadable. After this returns,
 * isProvisioned()/isLockdownActive() are false. APPROTECT is NOT touched
 * (its lockout is permanent on silicon where it engaged).
 */
void removeLockdownArtifacts();

/**
 * Returns a short string describing why the device is locked (set during initLocked()).
 * Useful for client-side diagnostics. Examples:
 *   "token_missing"      - no unlock token file found
 *   "token_wrong_size"   - token file exists but is corrupt
 *   "token_bad_magic"    - wrong magic bytes
 *   "token_hmac_fail"    - HMAC mismatch (tampered or wrong device)
 *   "token_boots_zero"   - boot count exhausted
 *   "token_expired"      - TTL expired
 *   "token_dek_fail"     - DEK decrypt failed
 *   "not_provisioned"    - no DEK file; needs first provisioning
 *   "ok"                 - unlocked successfully via token
 */
const char *getLockReason();

/** Boots remaining in the current unlock token (0 if not unlocked or last boot consumed). */
uint8_t getBootsRemaining();

/** Unix epoch at which the current unlock token expires (0 = no time limit or not unlocked). */
uint32_t getValidUntilEpoch();

/** Seconds remaining before next passphrase attempt is allowed (0 = can attempt now). */
uint32_t getBackoffSecondsRemaining();

// ---------------------------------------------------------------------------
// Uptime-based session limit
// ---------------------------------------------------------------------------
//
// Independent of the wall-clock and boot-count TTLs on the token. Caps how
// long a single auto-unlocked session can keep storage unlocked, measured
// in firmware millis() since the unlock. Reboot resets the counter, so an
// attacker who power-cycles to dodge the timer still burns a boot count.
// Combined hard cap: bootsRemaining * sessionMaxSeconds total exposure.
//
// Uptime (not wall-clock) by design: an attacker pulling the RTC backup
// battery and spoofing GPS to roll the clock back cannot defeat this -
// we never read getValidTime() for session enforcement. The check only
// engages when sessionMaxSeconds is non-zero, so 0 = unlimited (the
// existing token-only behavior, suitable for tower/infra nodes).

/// Start a session timer. Called after a successful passphrase unlock.
/// maxSeconds = 0 disables the timer for this session.
void setSession(uint32_t maxSeconds);

/// True if a session timer is set and has elapsed. Idempotent - call
/// from the main loop on a low-frequency tick.
bool isSessionExpired();

/// Seconds remaining in the current session. 0 if no timer is set, or if
/// the timer has expired (use isSessionExpired() to distinguish).
uint32_t getSessionRemainingSeconds();

/// Consume one boot from the on-flash token (the rollback ledger) and
/// re-arm the session timer in place - no reboot. Called from the main
/// loop when a session expires AND there is still budget. Decrements
/// bootsRemaining on flash (delete-and-rewrite of the token file, or
/// outright deletion if the new count is 0). Returns the new boot
/// count. Caller should check getBootsRemaining() == 0 before this
/// call: when zero, the budget is exhausted and a hard lock + reboot
/// should be issued instead.
uint8_t consumeSessionBoot();

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
bool encryptAndWrite(const char *filename, const uint8_t *plaintext, size_t plaintextLen, bool fullAtomic = false);

/**
 * Migrate a plaintext proto file to encrypted format in-place.
 * Returns true on success or if already encrypted.
 */
bool migrateFile(const char *filename);

} // namespace EncryptedStorage

#endif // MESHTASTIC_ENCRYPTED_STORAGE
