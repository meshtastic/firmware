#pragma once

#include "Observer.h"
#include "concurrency/OSThread.h"
#include "freertosinc.h"

#ifdef InputBrokerDebug
#define LOG_INPUT(...) LOG_DEBUG(__VA_ARGS__)
#else
#define LOG_INPUT(...)
#endif

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
    INPUT_BROKER_FACTORY_RST = 0x9a,
    INPUT_BROKER_SHUTDOWN = 0x9b,
    INPUT_BROKER_GPS_TOGGLE = 0x9e,
    INPUT_BROKER_PRIVACY_TOGGLE = 0x9f, // GPS and buzzer off together, and back on together
    INPUT_BROKER_SEND_PING = 0xaf,
    INPUT_BROKER_FN_F1 = 0xf1,
    INPUT_BROKER_FN_F2 = 0xf2,
    INPUT_BROKER_FN_F3 = 0xf3,
    INPUT_BROKER_FN_F4 = 0xf4,
    INPUT_BROKER_FN_F5 = 0xf5,
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

// Which physical joystick/gamepad button produced an event, carried in InputEvent::kbchar
// beside the action it is mapped to. Several buttons may share one action, so this is what
// lets a game tell SELECT-pressed-on-Y from SELECT-pressed-on-B and use more inputs than the
// handful of actions the broker defines.
//
// The value is the button's evdev code offset into a reserved kbchar range. 0x120..0x13f spans
// both the classic joystick codes (BTN_TRIGGER..BTN_DEAD) and the modern gamepad ones
// (BTN_SOUTH..BTN_THUMBR). The range deliberately misses printable ASCII (0x20-0x7e, which
// CannedMessages appends to a message) and every INPUT_BROKER_MSG_ value above: SystemCommands
// switches on kbchar without looking at inputEvent, so a collision there would toggle Bluetooth
// or reboot the node rather than move a paddle.
#define INPUT_BROKER_MSG_JOY_BUTTON_FIRST 0xC0
#define INPUT_BROKER_MSG_JOY_BUTTON_LAST 0xDF
#define INPUT_BROKER_JOY_CODE_FIRST 0x120
#define INPUT_BROKER_JOY_CODE_LAST 0x13F

// evdev button code -> the kbchar that reports it, or 0 for a code outside the encodable range
// (0 is also "no button", which is what every non-joystick source leaves in kbchar).
constexpr unsigned char joyButtonToKbchar(int code)
{
    return (code >= INPUT_BROKER_JOY_CODE_FIRST && code <= INPUT_BROKER_JOY_CODE_LAST)
               ? (unsigned char)(INPUT_BROKER_MSG_JOY_BUTTON_FIRST + (code - INPUT_BROKER_JOY_CODE_FIRST))
               : 0;
}

// True if this kbchar names a gamepad button, i.e. the event came from a real button press
// rather than from a D-pad axis (which has no button and leaves kbchar 0) or a keyboard key.
// Lets a consumer treat "LEFT from a shoulder button" differently from "LEFT from the D-pad"
// without hardcoding one pad's button codes.
constexpr bool isJoyButton(unsigned char kbchar)
{
    return kbchar >= INPUT_BROKER_MSG_JOY_BUTTON_FIRST && kbchar <= INPUT_BROKER_MSG_JOY_BUTTON_LAST;
}

// The Start button. Map it to "select" like any other button: it selects normally, and a consumer
// that wants Start specifically (GamesModule pauses on it) picks it out of kbchar. 0x129
// (BTN_BASE4) is Start on the classic 10-button pads, 0x13b (BTN_START) on modern gamepads.
constexpr bool isJoyStartButton(unsigned char kbchar)
{
    return kbchar == joyButtonToKbchar(0x129) || kbchar == joyButtonToKbchar(0x13b);
}

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
    virtual ~InputPollable() = default;
    virtual void pollOnce() = 0;
};

class InputBroker : public Observable<const InputEvent *>
{
    CallbackObserver<InputBroker, const InputEvent *> inputEventObserver =
        CallbackObserver<InputBroker, const InputEvent *>(this, &InputBroker::handleInputEvent);

  public:
    InputBroker();
    bool menuMode = true;
    void registerSource(Observable<const InputEvent *> *source);
    void injectInputEvent(const InputEvent *event) { handleInputEvent(event); }
#if defined(HAS_FREE_RTOS) && !defined(ARCH_RP2040)
    void requestPollSoon(InputPollable *pollable);
    void queueInputEvent(const InputEvent *event);
    void processInputEventQueue();
#endif
    void Init();

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
extern bool runASAP;