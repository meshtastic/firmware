#pragma once

#include "BBQ10Keyboard.h"
#include "InputBroker.h"
#include "MPR121Keyboard.h"
#include "Wire.h"
#include "concurrency/OSThread.h"

class TCA8418KeyboardBase;

class KbI2cBase : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    explicit KbI2cBase(const char *name);
    void toggleBacklight(bool on);

  protected:
    virtual int32_t runOnce() override;

  private:
    const char *_originName;

    TwoWire *i2cBus = 0;

#if HAS_BBQ10_KEYBOARD
    BBQ10Keyboard Q10keyboard;
#endif
#if HAS_MPR121_KEYBOARD
    MPR121Keyboard MPRkeyboard;
#endif
    TCA8418KeyboardBase &TCAKeyboard;
    bool is_sym = false;
};