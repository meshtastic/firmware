#pragma once

#include "BBQ10Keyboard.h"
#include "InputBroker.h"
#include "MPR121Keyboard.h"
#include "Wire.h"
#include "concurrency/OSThread.h"

#include <memory>

class TCA8418KeyboardBase;

class KbI2cBase : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    explicit KbI2cBase(const char *name);
    // Out-of-line: TCA8418KeyboardBase is only forward-declared here, so the unique_ptr
    // deleter must be instantiated in the .cpp where the type is complete
    ~KbI2cBase();
    void toggleBacklight(bool on);

  protected:
    virtual int32_t runOnce() override;

  private:
    const char *_originName;

    TwoWire *i2cBus = 0;

    BBQ10Keyboard Q10keyboard;
    MPR121Keyboard MPRkeyboard;
    std::unique_ptr<TCA8418KeyboardBase> TCAKeyboard;
    bool is_sym = false;
};