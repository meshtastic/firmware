#pragma once

#include "BBQ10Keyboard.h"
#include "InputBroker.h"
#include "MPR121Keyboard.h"
#include "Wire.h"
#include "concurrency/OSThread.h"
#include "kbInterrupt.h"

class TCA8418KeyboardBase;

class KbI2cBase : public Observable<const InputEvent *>, public KbInterruptObserver, public concurrency::OSThread
{
  public:
    explicit KbI2cBase(const char *name);

  protected:
    virtual int32_t runOnce() override;
    virtual int onNotify(KbInterruptObservable* src) override;

  private:
    const char *_originName;

    TwoWire *i2cBus = 0;

    BBQ10Keyboard Q10keyboard;
    MPR121Keyboard MPRkeyboard;
    TCA8418KeyboardBase &TCAKeyboard;
    volatile uint8_t pendingInterruptCount = 0;
    bool is_sym = false;
};