#include "TCA8418KeyboardBase.h"

class TLoraPagerKeyboard : public TCA8418KeyboardBase
{
  public:
    TLoraPagerKeyboard();
    void reset(void);
    void setBacklight(bool on) override;
    virtual ~TLoraPagerKeyboard() {}

  protected:
    void pressed(uint8_t key) override;
    void released(uint8_t key) override;
    int8_t keyToIndex(uint8_t key);
    void hapticFeedback(void);

    uint8_t keyToModifierFlag(uint8_t key);
    bool isModifierKey(uint8_t key);
    void toggleBacklight(bool off = false);

  private:
    uint8_t modifierFlag; // Flag to indicate if a modifier key is pressed
    uint8_t pressedKeysCount;
    bool onlyOneModifierPressed;
    bool persistedPreviousModifier;
    uint32_t brightness = 0;
};
