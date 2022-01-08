#pragma once
#include "SinglePortPlugin.h"
#include "HardwareInput.h"

enum RotaryEncoderInterruptBaseStateType
{
    EVENT_OCCURRED,
    EVENT_CLEARED
};

enum RotaryEncoderInterruptBaseActionType
{
    ACTION_NONE,
    ACTION_PRESSED,
    ACTION_CW,
    ACTION_CCW
};

class RotaryEncoderInterruptBase :
    public SinglePortPlugin,
    public Observable<const InputEvent *>,
    private concurrency::OSThread
{
  public:
    RotaryEncoderInterruptBase(
        uint8_t pinA, uint8_t pinB, uint8_t pinPress,
        char eventCw, char eventCcw, char eventPressed,
//        std::function<void(void)> onIntA, std::function<void(void)> onIntB, std::function<void(void)> onIntPress);
        void (*onIntA)(), void (*onIntB)(), void (*onIntPress)());
    void intPressHandler();
    void intAHandler();
    void intBHandler();

  protected:
    virtual int32_t runOnce();
    volatile RotaryEncoderInterruptBaseStateType rotaryStateCW = EVENT_CLEARED;
    volatile RotaryEncoderInterruptBaseStateType rotaryStateCCW = EVENT_CLEARED;
    volatile int rotaryLevelA = LOW;
    volatile int rotaryLevelB = LOW;
    volatile RotaryEncoderInterruptBaseActionType action = ACTION_NONE;

  private:
    uint8_t _pinA;
    uint8_t _pinB;
    char _eventCw;
    char _eventCcw;
    char _eventPressed;
};

RotaryEncoderInterruptBase *RotaryEncoderInterruptBase;