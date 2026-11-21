/*
 * Native stand-in for EncryptedStorage.
 *
 * The real implementation is nRF52-only by design - it hard-errors off ARCH_NRF52 because it
 * needs CC310 - so [env:coverage-lockdown] excludes it from the build and links this instead.
 * The redaction tests do not exercise crypto: what they need is for isLockdownActive() and
 * isUnlocked() to be steerable, so the compile-time gates in PhoneAPI::getFromRadio() are
 * reachable. Everything else is a no-op that fails safe.
 *
 * isLockdownActive() keeps the real semantics - "a DEK file exists" - so tests drive it by
 * creating and removing /prefs/.dek, exactly as the firmware decides it.
 */

#include "configuration.h"

#ifdef MESHTASTIC_ENCRYPTED_STORAGE

#include "FSCommon.h"
#include "security/EncryptedStorage.h"
#include <cstring>

namespace EncryptedStorage
{

static const char *kDekFilename = "/prefs/.dek";
static bool s_unlocked = true; // storage unlocked; per-connection auth is the gate under test
static const char *s_lockReason = "ok";

void initLocked() {}

bool isProvisioned()
{
#ifdef FSCom
    return FSCom.exists(kDekFilename);
#else
    return false;
#endif
}

bool isLockdownActive()
{
    return isProvisioned();
}

bool isUnlocked()
{
    return s_unlocked;
}

bool provisionPassphrase(const uint8_t *, size_t, uint8_t, uint32_t, uint32_t)
{
    s_unlocked = true;
    return true;
}

bool unlockWithPassphrase(const uint8_t *, size_t, uint8_t, uint32_t, uint32_t)
{
    s_unlocked = true;
    return true;
}

void lockNow()
{
    s_unlocked = false;
}

void secureWipeKeys()
{
    s_unlocked = false;
}

void removeLockdownArtifacts()
{
#ifdef FSCom
    FSCom.remove(kDekFilename);
#endif
}

const char *getLockReason()
{
    return s_lockReason;
}

uint8_t getBootsRemaining()
{
    return TOKEN_DEFAULT_BOOTS;
}

uint32_t getValidUntilEpoch()
{
    return 0;
}

uint32_t getBackoffSecondsRemaining()
{
    return 0;
}

void setSession(uint32_t) {}

bool isSessionExpired()
{
    return false;
}

uint8_t consumeSessionBoot()
{
    return 0;
}

// File helpers: the tests use plaintext prefs, so report "not encrypted" and pass writes
// through unchanged rather than pretending to encrypt.
bool isEncrypted(const char *)
{
    return false;
}

bool readAndDecrypt(const char *, uint8_t *, size_t, size_t &outLen)
{
    outLen = 0;
    return false;
}

bool encryptAndWrite(const char *, const uint8_t *, size_t, bool)
{
    return false;
}

bool migrateFile(const char *)
{
    return true;
}

bool migrateFileToPlaintext(const char *)
{
    return true;
}

} // namespace EncryptedStorage

#endif // MESHTASTIC_ENCRYPTED_STORAGE
