#include "TCA8418KeyboardBase.h"

class TLoraPagerKeyboard : public TCA8418KeyboardBase
{
  public:
    TLoraPagerKeyboard();
    void setBacklight(bool on) override{};

  protected:
    void pressed(uint8_t key) override{};
    void released(void) override{};
};
