#pragma once

#include "SinglePortModule.h" // TODO: what header file to include?
#include "InputBroker.h"

class UpDownInterruptBase :
    public Observable<const InputEvent *>
{
  public:
    explicit UpDownInterruptBase(
        const char *name);
    void init(
      uint8_t pinDown, uint8_t pinUp, uint8_t pinPress,
      char eventDown, char eventUp, char eventPressed,
        void (*onIntDown)(), void (*onIntUp)(), void (*onIntPress)());
    void intPressHandler();
    void intDownHandler();
    void intUpHandler();

  private:
    uint8_t _pinDown = 0;
    uint8_t _pinUp = 0;
    char _eventDown = InputEventChar_KEY_NONE;
    char _eventUp = InputEventChar_KEY_NONE;
    char _eventPressed = InputEventChar_KEY_NONE;
    const char *_originName;
};
