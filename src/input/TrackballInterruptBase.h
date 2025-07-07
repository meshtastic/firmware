#pragma once

#include "InputBroker.h"
#include "mesh/NodeDB.h"

#ifndef TB_DIRECTION
#if ARCH_PORTDUINO
#include "PortduinoGlue.h"
#define TB_DIRECTION (PinStatus) settingsMap[tbDirection]
#else
#define TB_DIRECTION RISING
#endif
#endif

class TrackballInterruptBase : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    explicit TrackballInterruptBase(const char *name);
    void init(uint8_t pinDown, uint8_t pinUp, uint8_t pinLeft, uint8_t pinRight, uint8_t pinPress, input_broker_event eventDown,
              input_broker_event eventUp, input_broker_event eventLeft, input_broker_event eventRight,
              input_broker_event eventPressed, void (*onIntDown)(), void (*onIntUp)(), void (*onIntLeft)(), void (*onIntRight)(),
              void (*onIntPress)());
    void intPressHandler();
    void intDownHandler();
    void intUpHandler();
    void intLeftHandler();
    void intRightHandler();
    uint32_t lastTime = 0;

    virtual int32_t runOnce() override;

  protected:
    enum TrackballInterruptBaseActionType {
        TB_ACTION_NONE,
        TB_ACTION_PRESSED,
        TB_ACTION_UP,
        TB_ACTION_DOWN,
        TB_ACTION_LEFT,
        TB_ACTION_RIGHT
    };
    uint8_t _pinDown = 0;
    uint8_t _pinUp = 0;
    uint8_t _pinLeft = 0;
    uint8_t _pinRight = 0;
    uint8_t _pinPress = 0;

    volatile TrackballInterruptBaseActionType action = TB_ACTION_NONE;

  private:
    input_broker_event _eventDown = INPUT_BROKER_NONE;
    input_broker_event _eventUp = INPUT_BROKER_NONE;
    input_broker_event _eventLeft = INPUT_BROKER_NONE;
    input_broker_event _eventRight = INPUT_BROKER_NONE;
    input_broker_event _eventPressed = INPUT_BROKER_NONE;
    const char *_originName;
    TrackballInterruptBaseActionType lastEvent = TB_ACTION_NONE;
};
