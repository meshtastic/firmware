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

    e.touchX = event.x;
    e.touchY = event.y;

    switch (event.touchEvent) {
    case TOUCH_ACTION_LEFT: {
        e.inputEvent = static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_RIGHT);
        break;
    }
    case TOUCH_ACTION_RIGHT: {
        e.inputEvent = static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_LEFT);
        break;
    }
    case TOUCH_ACTION_UP: {
        e.inputEvent = static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_UP);
        break;
    }
    case TOUCH_ACTION_DOWN: {
        e.inputEvent = static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_DOWN);
        break;
    }
    case TOUCH_ACTION_DOUBLE_TAP: {
        e.inputEvent = static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_SELECT);
        break;
    }
    case TOUCH_ACTION_LONG_PRESS: {
        e.inputEvent = static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_CANCEL);
        break;
    }
    case TOUCH_ACTION_TAP: {
        if (moduleConfig.external_notification.enabled && (externalNotificationModule->nagCycleCutoff != UINT32_MAX)) {
            externalNotificationModule->stopNow();
        } else {
            powerFSM.trigger(EVENT_INPUT);
        }
        break;
    }
    default:
        return;
    }
    this->notifyObservers(&e);
}