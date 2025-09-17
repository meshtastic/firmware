#include "TCA8418KeyboardBase.h"

/**
 * @brief 3x4 keypad with 3 columns and 4 rows
 */
class TCA8418Keyboard : public TCA8418KeyboardBase
{
  public:
    TCA8418Keyboard();
    void reset(void) override;
    void setBacklight(bool on) override;

  protected:
    void pressed(uint8_t key) override;
    void released(void) override;

    int8_t last_key;
    int8_t next_key;
    uint32_t last_tap;
    uint8_t char_idx;
    int32_t tap_interval;
    bool should_backspace;
};
