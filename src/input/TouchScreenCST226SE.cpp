#include "TouchScreenCST226SE.h"
#include "variant.h"

#ifdef HAS_CST226SE

#include "PowerFSM.h"
#include "Wire.h"
#include "configuration.h"
#include "modules/ExternalNotificationModule.h"

volatile bool CST_IRQ = false;

TouchScreenCST226SE *TouchScreenCST226SE::instance = nullptr;
TouchScreenCST226SE *touchScreenCST226SE;

TouchScreenCST226SE::TouchScreenCST226SE(uint16_t width, uint16_t height, bool (*getTouch)(int16_t *, int16_t *))
    : TouchScreenBase("touchscreen1", width, height), _getTouch(getTouch)
{
    instance = this;
}

void TouchScreenCST226SE::init()
{
    for (uint8_t addr : PossibleAddresses) {
        if (touch.begin(Wire, addr, I2C_SDA, I2C_SCL)) {
            i2cAddress = addr;

            // #ifdef TOUCHSCREEN_INT
            //             pinMode(TOUCHSCREEN_INT, INPUT);
            //             attachInterrupt(
            //                 TOUCHSCREEN_INT, [] { CST_IRQ = true; }, RISING);
            // #endif

            LOG_DEBUG("CST226SE init OK at address 0x%02X", addr);
            return;
        }
    }

    LOG_ERROR("CST226SE init failed at all known addresses");
}

bool TouchScreenCST226SE::getTouch(int16_t &x, int16_t &y)
{
    if (!touch.isPressed())
        return false;

    int16_t x_array[1], y_array[1];
    uint8_t count = touch.getPoint(x_array, y_array, 1);
    if (count > 0) {
        x = x_array[0];
        y = y_array[0];
        return true;
    }

    return false;
}

bool TouchScreenCST226SE::forwardGetTouch(int16_t *x, int16_t *y)
{
    if (instance) {
        return instance->getTouch(*x, *y);
        LOG_DEBUG("TouchScreen touched %dx %dy", x, y);
    }

    return false;
}

void TouchScreenCST226SE::onEvent(const TouchEvent &event)
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

#endif