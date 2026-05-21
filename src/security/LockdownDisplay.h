#pragma once

#include <cstdint>

namespace meshtastic_security
{

#ifdef MESHTASTIC_LOCKDOWN

/**
 * Display privacy policy for hardened lockdown builds.
 *
 * Renderers (Screen, InkHUD, niche graphics, device-ui) should consult
 * shouldRedactDisplay() at their top-level draw entry point. When true,
 * render a static "locked" view (e.g. just product name + battery), NOT
 * the normal node list / messages / GPS / channel content.
 *
 * Redaction triggers on either of two conditions:
 *
 *   1. Encrypted storage is locked (no DEK in RAM). NodeDB holds only
 *      defaults, but the explicit gate also keeps cached/stale UI state
 *      from leaking. Only firmware built with MESHTASTIC_ENCRYPTED_STORAGE
 *      has a storage state to check; elsewhere this condition is false.
 *
 *   2. The screen-lock latch is set. This is a separate state from
 *      storage-locked: the device stays fully functional on the mesh,
 *      only the display is gated. The latch is set by lockScreen() when
 *      the stock idle timeout powers the screen off (hooked in
 *      Screen::setOn) — so it reuses config.display.screen_on_secs
 *      rather than running a second timer. It is cleared only by
 *      unlockScreen(), called from PhoneAPI's lockdown_auth handler when
 *      a client authenticates with the passphrase over any transport.
 *      Button/joystick input can wake the backlight but does NOT clear
 *      the latch — the woken screen shows the LOCKED frame, not content.
 *      This closes the "operator walked away from an unlocked device"
 *      leak without conflating it with the storage-lock security state.
 *
 *      The latch starts TRUE at boot so a token-auto-unlocked cold boot
 *      comes up redacted — otherwise an attacker holding a screen-locked
 *      device could power-cycle it (RAM latch resets) to recover a
 *      content screen. After any boot, the operator must authenticate
 *      from a client to reveal content.
 *
 * CURRENT COVERAGE
 *   - graphics/Screen.cpp (OLED via OLEDDisplayUi): GATED, renders a centered
 *     "LOCKED" + battery when shouldRedactDisplay() is true.
 *
 * KNOWN GAPS — these renderers still leak content under lockdown
 *   - graphics/InkHUD/        (e-ink rich UI on supported boards)
 *   - graphics/niche/         (TFT niche graphics)
 *   - meshtastic/device-ui    (T-Deck/TFT, separate submodule)
 *
 * Each of those does not flow through Screen::updateUiFrame() and therefore
 * does not yet consult this policy. Operators using lockdown builds on
 * InkHUD/niche/device-ui hardware should treat the screen as an
 * always-on plaintext leak surface until those renderers are wired up.
 * Wiring the other renderers is a follow-up effort once this lands.
 */
bool shouldRedactDisplay();

/// Set the screen-lock latch. Called from Screen::setOn(false) when the
/// display powers off (idle timeout, shutdown, deep sleep). Idempotent.
void lockScreen();

/// Clear the screen-lock latch. Called from PhoneAPI's lockdown_auth
/// handler after a client authenticates with the passphrase.
void unlockScreen();

#else

inline bool shouldRedactDisplay()
{
    return false;
}
inline void lockScreen() {}
inline void unlockScreen() {}

#endif // MESHTASTIC_LOCKDOWN

} // namespace meshtastic_security
