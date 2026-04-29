#include "configuration.h"

#ifdef MESHTASTIC_LOCKDOWN

#include "LockdownDisplay.h"

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
#include "security/EncryptedStorage.h"
#endif

namespace meshtastic_security
{

// Screen-lock latch. Set when the display powers off (idle timeout etc.),
// cleared only when a client authenticates with the passphrase. Separate
// from storage-lock state: the device keeps routing while this is set,
// only the display is gated.
//
// Initialised to true so that even a token-auto-unlocked cold boot comes
// up with a redacted screen. Otherwise an attacker holding a screen-locked
// device could simply power-cycle it (RAM latch resets) to get back to a
// content screen. Operator must authenticate from a client to reveal
// content after any boot.
static bool s_screenLocked = true;

bool shouldRedactDisplay()
{
#ifdef MESHTASTIC_ENCRYPTED_STORAGE
    if (!EncryptedStorage::isUnlocked())
        return true;
#endif
    return s_screenLocked;
}

void lockScreen()
{
    s_screenLocked = true;
}

void unlockScreen()
{
    s_screenLocked = false;
}

} // namespace meshtastic_security

#endif // MESHTASTIC_LOCKDOWN
