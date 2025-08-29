#include "TCA8418KeyboardBase.h"

class TLoraPagerKeyboard : public TCA8418KeyboardBase
{
  public:
    TLoraPagerKeyboard();
    void reset(void);
    void trigger(void) override;
    void setBacklight(bool on) override;
    virtual ~TLoraPagerKeyboard() {}

  protected:
    void pressed(uint8_t key) override;
    void released(void) override;
    void hapticFeedback(void);

    void updateModifierFlag(uint8_t key);
    bool isModifierKey(uint8_t key);
    void toggleBacklight(bool off = false);

  private:
    uint8_t modifierFlag;        // Flag to indicate if a modifier key is pressed
    uint32_t last_modifier_time; // Timestamp of the last modifier key press
    int8_t last_key;
    int8_t next_key;
    uint32_t last_tap;
    uint8_t char_idx;
    int32_t tap_interval;
};
