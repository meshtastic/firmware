#include "TouchScreenImpl1.h"
#include "InputBroker.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "modules/ExternalNotificationModule.h"

#if ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#endif

TouchScreenImpl1 *touchScreenImpl1;

TouchScreenImpl1::TouchScreenImpl1(uint16_t width, uint16_t height, bool (*getTouch)(int16_t *, int16_t *))
    : TouchScreenBase("touchscreen1", width, height), _getTouch(getTouch)
{
}

void TouchScreenImpl1::init()
{
#if ARCH_PORTDUINO
    if (settingsMap[touchscreenModule]) {
        TouchScreenBase::init(true);
        inputBroker->registerSource(this);
    } else {
        TouchScreenBase::init(false);
    }
#elif !HAS_TOUCHSCREEN
    TouchScreenBase::init(false);
    return;
#else
    TouchScreenBase::init(true);
    inputBroker->registerSource(this);
#endif
}

bool TouchScreenImpl1::getTouch(int16_t &x, int16_t &y)
{
    return _getTouch(&x, &y);
}

/**
 * @brief forward touchscreen event
 *
 * @param event
 *
 * The touchscreen events are translated to input events and reversed
 */
void TouchScreenImpl1::onEvent(const TouchEvent &event)
{
    InputEvent e;
    e.source = event.source;
    e.kbchar = 0;
    e.touchX = event.x;
    e.touchY = event.y;

    switch (event.touchEvent) {
    case TOUCH_ACTION_LEFT: {
        e.inputEvent = INPUT_BROKER_LEFT;
        break;
    }
    case TOUCH_ACTION_RIGHT: {
        e.inputEvent = INPUT_BROKER_RIGHT;
        break;
    }
    case TOUCH_ACTION_UP: {
        e.inputEvent = INPUT_BROKER_UP;
        break;
    }
    case TOUCH_ACTION_DOWN: {
        e.inputEvent = INPUT_BROKER_DOWN;
        break;
    }
    case TOUCH_ACTION_LONG_PRESS: {
        e.inputEvent = INPUT_BROKER_SELECT;
        break;
    }
    case TOUCH_ACTION_TAP: {
        e.inputEvent = INPUT_BROKER_USER_PRESS;
        break;
    }
    default:
        return;
    }
    this->notifyObservers(&e);
}