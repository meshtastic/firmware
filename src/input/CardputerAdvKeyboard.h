#include "TCA8418KeyboardBase.h"

class CardputerAdvKeyboard : public TCA8418KeyboardBase
{
  public:
    CardputerAdvKeyboard();
    void reset(void);
    void trigger(void) override;
    virtual ~CardputerAdvKeyboard() {}

  protected:
    void pressed(uint8_t key) override;
    void released(void) override;

    void updateModifierFlag(uint8_t key);
    bool isModifierKey(uint8_t key);

  private:
    uint8_t modifierFlag;        // Flag to indicate if a modifier key is pressed
    uint32_t last_modifier_time; // Timestamp of the last modifier key press
    int8_t last_key;
    int8_t next_key;
    uint32_t last_tap;
    uint8_t char_idx;
    int32_t tap_interval;
};
