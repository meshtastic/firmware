#pragma once

#include "configuration.h"

#if defined(GAT562_T9_KEYBOARD)

#include "TCA8418KeyboardBase.h"

class GAT562T9Keyboard : public TCA8418KeyboardBase
{
  public:
    GAT562T9Keyboard();

  protected:
    void pressed(uint8_t key) override;
    void released(void) override;

  private:
    uint8_t last_key;
    uint8_t next_key;
    uint32_t last_tap;
    uint8_t char_idx;
    int32_t tap_interval;
    bool should_backspace;
};

#endif
