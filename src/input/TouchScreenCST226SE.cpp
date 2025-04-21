#include "TouchScreenCST226SE.h"
#include "variant.h"

#ifdef HAS_CST226SE

#ifndef TOUCH_IRQ
#define TOUCH_IRQ -1
#endif

#include "PowerFSM.h"
#include "Wire.h"
#include "configuration.h"
#include "modules/ExternalNotificationModule.h"

volatile bool isPressed = false;

TouchScreenCST226SE *TouchScreenCST226SE::instance = nullptr;
TouchScreenCST226SE *touchScreenCST226SE;

TouchScreenCST226SE::TouchScreenCST226SE(uint16_t width, uint16_t height) : TouchScreenBase("CST226", width, height)
{
    instance = this;
}

void TouchScreenCST226SE::init()
{
    touch.setPins(-1, TOUCH_IRQ);
    touch.setTouchDrvModel(TouchDrv_CST226);
    for (uint8_t addr : PossibleAddresses) {
        if (touch.begin(Wire, addr, I2C_SDA, I2C_SCL)) {
            i2cAddress = addr;
            if (TOUCH_IRQ != -1) {
                pinMode(TOUCH_IRQ, INPUT_PULLUP);
                attachInterrupt(
                    TOUCH_IRQ, []() { isPressed = true; }, FALLING);
            }
            LOG_DEBUG("CST226SE init OK at address 0x%02X", addr);
            inputBroker->registerSource(this);
            return;
        }
    }
    LOG_ERROR("CST226SE init failed at all known addresses");
}

bool TouchScreenCST226SE::getTouch(int16_t &x, int16_t &y)
{
    int16_t x_array[1], y_array[1];
    uint8_t touched = touch.getPoint(x_array, y_array, 1);
    if (touched > 0) {
        y = x_array[0];
        x = (TFT_WIDTH - y_array[0]);
        // Check bounds
        if (x < 0 || x >= TFT_WIDTH || y < 0 || y >= TFT_HEIGHT) {
            return false;
        }
        return true; // Valid touch detected
    }
    return false; // No valid touch data
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