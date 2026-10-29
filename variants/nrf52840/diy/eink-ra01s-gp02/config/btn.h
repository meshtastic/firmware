// =============================================================================
// config/btn.h — Button Role → Meshtastic Name Mapping
// =============================================================================
// Maps hardware button pins (PIN_BTN_F1..F6) to the names expected by
// SixKeyNavInput and the Meshtastic InputBroker framework.
//
// All six buttons are handled directly by SixKeyNavInput (bypassing the
// ButtonThread singletons BUTTON_PIN / ALT_BUTTON_PIN / CANCEL_BUTTON_PIN),
// so those framework macros are intentionally left undefined.
// =============================================================================

#pragma once

#include "variant.h"

// ---------------------------------------------------------------------------
// SixKeyNavInput expects these names
// ---------------------------------------------------------------------------
#define PIN_BUTTON_RIGHT PIN_BTN_F3
#define PIN_BUTTON_DOWN PIN_BTN_F4
#define PIN_BUTTON_LEFT PIN_BTN_F2
#define PIN_BUTTON_FN PIN_BTN_F5
#define PIN_BUTTON_ENTER PIN_BTN_F6
#define PIN_BUTTON_UP PIN_BTN_F1
