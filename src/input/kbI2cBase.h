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
    void setBacklightLevel(uint8_t level);
    void suspendBacklightForSleep(bool sleeping);

  protected:
    virtual int32_t runOnce() override;

  private:
    const char *_originName;

    TwoWire *i2cBus = 0;

    BBQ10Keyboard Q10keyboard;
    MPR121Keyboard MPRkeyboard;
    TCA8418KeyboardBase &TCAKeyboard;
    bool is_sym = false;

    bool kbBacklightEnabled = true;
    uint8_t kbBacklightLevel = 127;
    bool kbBacklightWasEnabledBeforeSleep = true;
};