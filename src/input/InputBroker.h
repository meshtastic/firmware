#pragma once

#include "Observer.h"
#include "freertosinc.h"

enum input_broker_event {
    INPUT_BROKER_NONE = 0,
    INPUT_BROKER_SELECT = 10,
    INPUT_BROKER_SELECT_LONG = 11,
    INPUT_BROKER_UP_LONG = 12,
    INPUT_BROKER_DOWN_LONG = 13,
    INPUT_BROKER_UP = 17,
    INPUT_BROKER_DOWN = 18,
    INPUT_BROKER_LEFT = 19,
    INPUT_BROKER_RIGHT = 20,
    INPUT_BROKER_CANCEL = 24,
    INPUT_BROKER_BACK = 27,
    INPUT_BROKER_USER_PRESS,
    INPUT_BROKER_ALT_PRESS,
    INPUT_BROKER_ALT_LONG,
    INPUT_BROKER_SHUTDOWN = 0x9b,
    INPUT_BROKER_GPS_TOGGLE = 0x9e,
    INPUT_BROKER_SEND_PING = 0xaf,
    INPUT_BROKER_MATRIXKEY = 0xFE,
    INPUT_BROKER_ANYKEY = 0xff

};

#define INPUT_BROKER_MSG_BRIGHTNESS_UP 0x11
#define INPUT_BROKER_MSG_BRIGHTNESS_DOWN 0x12
#define INPUT_BROKER_MSG_REBOOT 0x90
#define INPUT_BROKER_MSG_MUTE_TOGGLE 0xac
#define INPUT_BROKER_MSG_FN_SYMBOL_ON 0xf1
#define INPUT_BROKER_MSG_FN_SYMBOL_OFF 0xf2
#define INPUT_BROKER_MSG_BLUETOOTH_TOGGLE 0xAA
#define INPUT_BROKER_MSG_TAB 0x09
#define INPUT_BROKER_MSG_EMOTE_LIST 0x8F

typedef struct _InputEvent {
    const char *source;
    input_broker_event inputEvent;
    unsigned char kbchar;
    uint16_t touchX;
    uint16_t touchY;
} InputEvent;

class InputPollable
{
  public:
    virtual void pollOnce() = 0;
};

class InputBroker : public Observable<const InputEvent *>
{
    CallbackObserver<InputBroker, const InputEvent *> inputEventObserver =
        CallbackObserver<InputBroker, const InputEvent *>(this, &InputBroker::handleInputEvent);

  public:
    InputBroker();
    void registerSource(Observable<const InputEvent *> *source);
    void injectInputEvent(const InputEvent *event) { handleInputEvent(event); }
#if defined(HAS_FREE_RTOS) && !defined(ARCH_RP2040)
    void requestPollSoon(InputPollable *pollable);
    void queueInputEvent(const InputEvent *event);
    void processInputEventQueue();
#endif

  protected:
    int handleInputEvent(const InputEvent *event);

  private:
#if defined(HAS_FREE_RTOS) && !defined(ARCH_RP2040)
    QueueHandle_t inputEventQueue;
    QueueHandle_t pollSoonQueue;
    TaskHandle_t pollSoonTask;
    static void pollSoonWorker(void *p);
#endif
};

extern InputBroker *inputBroker;