#include "configuration.h"

#ifdef MESHTASTIC_LOCKDOWN

#include "LockdownDisplay.h"

#ifdef MESHTASTIC_ENCRYPTED_STORAGE
#include "security/EncryptedStorage.h"
#endif

#include <atomic>

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
//
// std::atomic so cross-task reads (PowerFSM / Screen / InputBroker) see
// writes immediately and the compiler is not free to speculate the load.
// Plain bool happens to work on single-core Cortex-M4 today but breaks
// silently the moment lockdown ports to ESP32 / RP2040 / LTO whole-program
// elision.
static std::atomic<bool> s_screenLocked{true};

bool shouldRedactDisplay()
{
#ifdef MESHTASTIC_ENCRYPTED_STORAGE
    // Lockdown not active (capable build, never provisioned or disabled):
    // never redact the display — behave like stock firmware.
    if (!EncryptedStorage::isLockdownActive())
        return false;
    if (!EncryptedStorage::isUnlocked())
        return true;
#endif
    return s_screenLocked.load(std::memory_order_relaxed);
}

void lockScreen()
{
    s_screenLocked.store(true, std::memory_order_relaxed);
}

void unlockScreen()
{
    s_screenLocked.store(false, std::memory_order_relaxed);
}

} // namespace meshtastic_security

#endif // MESHTASTIC_LOCKDOWN
