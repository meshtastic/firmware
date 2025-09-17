#pragma once

#include "InputBroker.h"
#include "mesh/NodeDB.h"

class UpDownInterruptBase : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    explicit UpDownInterruptBase(const char *name);
    void init(uint8_t pinDown, uint8_t pinUp, uint8_t pinPress, input_broker_event eventDown, input_broker_event eventUp,
              input_broker_event eventPressed, input_broker_event eventPressedLong, input_broker_event eventUpLong,
              input_broker_event eventDownLong, void (*onIntDown)(), void (*onIntUp)(), void (*onIntPress)(),
              unsigned long updownDebounceMs = 50);
    void intPressHandler();
    void intDownHandler();
    void intUpHandler();

    int32_t runOnce() override;

  protected:
    enum UpDownInterruptBaseActionType {
        UPDOWN_ACTION_NONE,
        UPDOWN_ACTION_PRESSED,
        UPDOWN_ACTION_PRESSED_LONG,
        UPDOWN_ACTION_UP,
        UPDOWN_ACTION_UP_LONG,
        UPDOWN_ACTION_DOWN,
        UPDOWN_ACTION_DOWN_LONG
    };

    volatile UpDownInterruptBaseActionType action = UPDOWN_ACTION_NONE;

    // Long press detection variables
    uint32_t pressStartTime = 0;
    uint32_t upStartTime = 0;
    uint32_t downStartTime = 0;
    bool pressDetected = false;
    bool upDetected = false;
    bool downDetected = false;
    uint32_t lastPressLongEventTime = 0;
    uint32_t lastUpLongEventTime = 0;
    uint32_t lastDownLongEventTime = 0;
    static const uint32_t LONG_PRESS_DURATION = 300;
    static const uint32_t LONG_PRESS_REPEAT_INTERVAL = 300;

  private:
    uint8_t _pinDown = 0;
    uint8_t _pinUp = 0;
    uint8_t _pinPress = 0;
    input_broker_event _eventDown = INPUT_BROKER_NONE;
    input_broker_event _eventUp = INPUT_BROKER_NONE;
    input_broker_event _eventPressed = INPUT_BROKER_NONE;
    input_broker_event _eventPressedLong = INPUT_BROKER_NONE;
    input_broker_event _eventUpLong = INPUT_BROKER_NONE;
    input_broker_event _eventDownLong = INPUT_BROKER_NONE;
    const char *_originName;

    unsigned long lastUpKeyTime = 0;
    unsigned long lastDownKeyTime = 0;
    unsigned long lastPressKeyTime = 0;
    unsigned long updownDebounceMs = 50;
    const unsigned long pressDebounceMs = 200;
};
