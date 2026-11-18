#pragma once

#include "TCA8418KeyboardBase.h"

class TDeckProKeyboard : public TCA8418KeyboardBase
{
  public:
    TDeckProKeyboard();
    void reset() override;
    void trigger() override;
    void setBacklight(bool on) override;

  protected:
    void pressed(uint8_t key) override;

  private:
    static uint8_t modifierForKey(uint8_t key);
    void releaseKey(uint8_t key);
    void toggleBacklight();

    uint8_t heldModifiers = 0;
    uint8_t latchedModifiers = 0;
    uint8_t usedModifiers = 0;
    uint32_t lastModifierMs = 0;
};
