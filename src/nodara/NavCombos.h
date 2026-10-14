#pragma once

#if defined(Nodara)

/**
 * NavCombos - Nodara variant combo-key handlers (Fn + direction/Enter).
 *
 * Invoked by SixKeyNavInput when Fn is held and another key is released.
 * Cancel (Fn) alone → INPUT_BROKER_CANCEL (screen off), not handled here.
 *
 * Combo mapping:
 *   Fn + Up    → toggle Bluetooth (persistent, triggers reboot)
 *   Fn + Down  → deep sleep (INPUT_BROKER_SHUTDOWN)
 *   Fn + Left  → unassigned
 *   Fn + Right → toggle GPS power (persistent)
 *   Fn + Enter → unassigned
 */

void navComboUp();
void navComboDown();
void navComboLeft();
void navComboRight();
void navComboEnter();

#endif // Nodara
