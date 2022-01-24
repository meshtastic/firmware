#pragma once

#include "SinglePortPlugin.h" // TODO: what header file to include?
#include "InputBroker.h"

enum RotaryEncoderInterruptBaseStateType
{
    ROTARY_EVENT_OCCURRED,
    ROTARY_EVENT_CLEARED
};

enum RotaryEncoderInterruptBaseActionType
{
    ROTARY_ACTION_NONE,
    ROTARY_ACTION_PRESSED,
    ROTARY_ACTION_CW,
    ROTARY_ACTION_CCW
};

class RotaryEncoderInterruptBase :
    public Observable<const InputEvent *>,
    private concurrency::OSThread
{
  public:
    RotaryEncoderInterruptBase(
        const char *name);
    void init(
      uint8_t pinA, uint8_t pinB, uint8_t pinPress,
      char eventCw, char eventCcw, char eventPressed,
//        std::function<void(void)> onIntA, std::function<void(void)> onIntB, std::function<void(void)> onIntPress);
        void (*onIntA)(), void (*onIntB)(), void (*onIntPress)());
    void intPressHandler();
    void intAHandler();
    void intBHandler();

  protected:
    virtual int32_t runOnce();
    RotaryEncoderInterruptBaseStateType intHandler(
        bool actualPinRaising,
        int otherPinLevel,
        RotaryEncoderInterruptBaseActionType action,
        RotaryEncoderInterruptBaseStateType state);

    volatile RotaryEncoderInterruptBaseStateType rotaryStateCW = ROTARY_EVENT_CLEARED;
    volatile RotaryEncoderInterruptBaseStateType rotaryStateCCW = ROTARY_EVENT_CLEARED;
    volatile int rotaryLevelA = LOW;
    volatile int rotaryLevelB = LOW;
    volatile RotaryEncoderInterruptBaseActionType action = ROTARY_ACTION_NONE;

  private:
    uint8_t _pinA = 0;
    uint8_t _pinB = 0;
    char _eventCw = InputEventChar_KEY_NONE;
    char _eventCcw = InputEventChar_KEY_NONE;
    char _eventPressed = InputEventChar_KEY_NONE;
    const char *_originName;
};
