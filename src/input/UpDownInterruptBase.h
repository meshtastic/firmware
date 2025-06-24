#pragma once

#include "InputBroker.h"
#include "mesh/NodeDB.h"

class UpDownInterruptBase : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    explicit UpDownInterruptBase(const char *name);
    void init(uint8_t pinDown, uint8_t pinUp, uint8_t pinPress, input_broker_event eventDown, input_broker_event eventUp,
              input_broker_event eventPressed, void (*onIntDown)(), void (*onIntUp)(), void (*onIntPress)(),
              unsigned long updownDebounceMs = 50);
    void intPressHandler();
    void intDownHandler();
    void intUpHandler();

    int32_t runOnce() override;

  protected:
    enum UpDownInterruptBaseActionType { UPDOWN_ACTION_NONE, UPDOWN_ACTION_PRESSED, UPDOWN_ACTION_UP, UPDOWN_ACTION_DOWN };

    volatile UpDownInterruptBaseActionType action = UPDOWN_ACTION_NONE;

  private:
    uint8_t _pinDown = 0;
    uint8_t _pinUp = 0;
    input_broker_event _eventDown = INPUT_BROKER_NONE;
    input_broker_event _eventUp = INPUT_BROKER_NONE;
    input_broker_event _eventPressed = INPUT_BROKER_NONE;
    const char *_originName;

    unsigned long lastUpKeyTime = 0;
    unsigned long lastDownKeyTime = 0;
    unsigned long lastPressKeyTime = 0;
    unsigned long updownDebounceMs = 50;
    const unsigned long pressDebounceMs = 200;
};
