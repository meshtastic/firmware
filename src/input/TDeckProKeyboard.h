#include "TCA8418KeyboardBase.h"

class TDeckProKeyboard : public TCA8418KeyboardBase
{
  public:
    TDeckProKeyboard();
    void reset(void) override;
    void trigger(void) override;
    void setBacklight(bool on) override;

  protected:
    void pressed(uint8_t key) override;
    void released(void) override;

    void updateModifierFlag(uint8_t key);
    bool isModifierKey(uint8_t key);
    void toggleBacklight(void);

  private:
    uint8_t modifierFlag;        // Flag to indicate if a modifier key is pressed
    uint32_t last_modifier_time; // Timestamp of the last modifier key press
    int8_t last_key;
    int8_t next_key;
    uint32_t last_tap;
    uint8_t char_idx;
    int32_t tap_interval;
};
