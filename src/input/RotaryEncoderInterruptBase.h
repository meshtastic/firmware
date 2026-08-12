#pragma once

#include "InputBroker.h"
#include "concurrency/OSThread.h"
#include "mesh/NodeDB.h"
#include <atomic>

enum RotaryEncoderInterruptBaseStateType { ROTARY_EVENT_OCCURRED, ROTARY_EVENT_CLEARED };
enum RotaryEncoderInterruptBaseActionType { ROTARY_ACTION_NONE, ROTARY_ACTION_PRESSED, ROTARY_ACTION_CW, ROTARY_ACTION_CCW };

class RotaryEncoderInterruptBase : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    explicit RotaryEncoderInterruptBase(const char *name);
    void init(uint8_t pinA, uint8_t pinB, uint8_t pinPress, input_broker_event eventCw, input_broker_event eventCcw,
              input_broker_event eventPressed, input_broker_event eventPressedLong,
              //        std::function<void(void)> onIntA, std::function<void(void)> onIntB, std::function<void(void)> onIntPress);
              void (*onIntA)(), void (*onIntB)(), void (*onIntPress)());
    void intPressHandler();
    void intAHandler();
    void intBHandler();

    // Turning while the button is held emits `cw` / `ccw` as kbchar and suppresses the press
    // event. Both default to 0, leaving rotation-while-pressed ignored.
    void setPressAndTurnChars(unsigned char cw, unsigned char ccw);

  protected:
    virtual int32_t runOnce() override;
    RotaryEncoderInterruptBaseStateType intHandler(bool actualPinRaising, int otherPinLevel,
                                                   RotaryEncoderInterruptBaseActionType action,
                                                   RotaryEncoderInterruptBaseStateType state);

    volatile RotaryEncoderInterruptBaseStateType rotaryStateCW = ROTARY_EVENT_CLEARED;
    volatile RotaryEncoderInterruptBaseStateType rotaryStateCCW = ROTARY_EVENT_CLEARED;
    volatile int rotaryLevelA = LOW;
    volatile int rotaryLevelB = LOW;
    volatile RotaryEncoderInterruptBaseActionType action = ROTARY_ACTION_NONE;
    // Detents counted while the button is held: positive clockwise, negative counter-clockwise.
    std::atomic<int32_t> pressAndTurnDelta{0};

  private:
    // pins and events
    uint8_t _pinA = 0;
    uint8_t _pinB = 0;
    uint8_t _pinPress = 0;
    input_broker_event _eventCw = INPUT_BROKER_NONE;
    input_broker_event _eventCcw = INPUT_BROKER_NONE;
    input_broker_event _eventPressed = INPUT_BROKER_NONE;
    input_broker_event _eventPressedLong = INPUT_BROKER_NONE;
    const char *_originName;

    // Press-and-turn characters, 0 when the gesture is not in use
    unsigned char _pressAndTurnCw = 0;
    unsigned char _pressAndTurnCcw = 0;
    bool pressAndTurnEnabled() const { return _pressAndTurnCw != 0 || _pressAndTurnCcw != 0; }

    // Long press detection variables
    uint32_t pressStartTime = 0;
    bool pressDetected = false;
    // Suppresses the press event on release once press-and-turn has fired.
    bool pressAndTurnFired = false;
    uint32_t lastPressLongEventTime = 0;
    unsigned long lastPressKeyTime = 0;
    static const uint32_t LONG_PRESS_DURATION = 300;      // ms
    static const uint32_t LONG_PRESS_REPEAT_INTERVAL = 0; // 0 = single-shot for rotary select
    const unsigned long pressDebounceMs = 200;            // ms
};
