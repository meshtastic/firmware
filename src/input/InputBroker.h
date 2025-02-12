#pragma once
#include "Observer.h"

#define ANYKEY 0xFF
#define MATRIXKEY 0xFE

#define INPUT_BROKER_MSG_BRIGHTNESS_UP 0x11
#define INPUT_BROKER_MSG_BRIGHTNESS_DOWN 0x12
#define INPUT_BROKER_MSG_REBOOT 0x90
#define INPUT_BROKER_MSG_SHUTDOWN 0x9b
#define INPUT_BROKER_MSG_GPS_TOGGLE 0x9e
#define INPUT_BROKER_MSG_MUTE_TOGGLE 0xac
#define INPUT_BROKER_MSG_SEND_PING 0xaf
#define INPUT_BROKER_MSG_DISMISS_FRAME 0x8b
#define INPUT_BROKER_MSG_LEFT 0xb4
#define INPUT_BROKER_MSG_UP 0xb5
#define INPUT_BROKER_MSG_DOWN 0xb6
#define INPUT_BROKER_MSG_RIGHT 0xb7
#define INPUT_BROKER_MSG_FN_SYMBOL_ON 0xf1
#define INPUT_BROKER_MSG_FN_SYMBOL_OFF 0xf2
#define INPUT_BROKER_MSG_BLUETOOTH_TOGGLE 0xAA

typedef struct _InputEvent {
    const char *source;
    char inputEvent;
    char kbchar;
    uint16_t touchX;
    uint16_t touchY;
} InputEvent;
class InputBroker : public Observable<const InputEvent *>
{
    CallbackObserver<InputBroker, const InputEvent *> inputEventObserver =
        CallbackObserver<InputBroker, const InputEvent *>(this, &InputBroker::handleInputEvent);

  public:
    InputBroker();
    void registerSource(Observable<const InputEvent *> *source);

  protected:
    int handleInputEvent(const InputEvent *event);
};

extern InputBroker *inputBroker;