#pragma once

#include "TCA8418KeyboardBase.h"

class TDeckProKeyboard : public TCA8418KeyboardBase
{
  public:
    TDeckProKeyboard();
    void reset() override;
    void trigger() override;
    void setBacklight(bool on) override;
#ifndef MESHTASTIC_INCLUDE_INKHUD
    // The keyboard light follows uiconfig.keyboard_backlight while the screen is on.
    void setScreenOn(bool on);
    // Applies the light's mode; returns ms until it next needs calling (UINT32_MAX: not until something changes).
    uint32_t updateBacklight();
#endif

  protected:
    void pressed(uint8_t key) override;

  private:
    static uint8_t modifierForKey(uint8_t key);
    void releaseKey(uint8_t key);
#ifndef MESHTASTIC_INCLUDE_INKHUD
    void backlightShortcut();

    bool screenOn = true;
    bool typingLit = false; // WHILE_TYPING: lit by a key press (or Alt+B) less than BACKLIGHT_TYPING_MS ago
    uint32_t lastTypingMs = 0;
    bool backlightLit = false; // what KB_BL_PIN is driven to
#endif

    uint8_t heldModifiers = 0;
    uint8_t latchedModifiers = 0;
    uint8_t usedModifiers = 0;
    uint32_t lastModifierMs = 0;
    uint32_t lastPopUs = 0;
};
