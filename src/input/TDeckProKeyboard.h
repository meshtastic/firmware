#include "TCA8418KeyboardBase.h"

class TDeckProKeyboard : public TCA8418KeyboardBase
{
  public:
    TDeckProKeyboard();
    void reset(void) override;
    void setBacklight(bool on) override;

  protected:
    void pressed(uint8_t key) override;
    void released(uint8_t key) override;
    int8_t keyToIndex(uint8_t key);

    uint8_t keyToModifierFlag(uint8_t key);
    bool isModifierKey(uint8_t key);
    void toggleBacklight(void);

  private:
    uint8_t modifierFlag;        // Flag to indicate if a modifier key is pressed
};
