#ifdef T_LORA_PAGER

#include "RotaryEncoderImpl.h"
#include "InputBroker.h"
#include "RotaryEncoder.h"

#define ORIGIN_NAME "RotaryEncoder"

RotaryEncoderImpl *rotaryEncoderImpl;

RotaryEncoderImpl::RotaryEncoderImpl()
{
    rotary = nullptr;
}

bool RotaryEncoderImpl::init()
{
    if (!moduleConfig.canned_message.updown1_enabled || moduleConfig.canned_message.inputbroker_pin_a == 0 ||
        moduleConfig.canned_message.inputbroker_pin_b == 0) {
        // Input device is disabled.
        return false;
    }

    eventCw = static_cast<input_broker_event>(moduleConfig.canned_message.inputbroker_event_cw);
    eventCcw = static_cast<input_broker_event>(moduleConfig.canned_message.inputbroker_event_ccw);
    eventPressed = static_cast<input_broker_event>(moduleConfig.canned_message.inputbroker_event_press);

    rotary = new RotaryEncoder(moduleConfig.canned_message.inputbroker_pin_a, moduleConfig.canned_message.inputbroker_pin_b,
                               moduleConfig.canned_message.inputbroker_pin_press);
    rotary->resetButton();

    interruptInstance = this;
    auto interruptHandler = []() { inputBroker->requestPollSoon(interruptInstance); };
    attachInterrupt(moduleConfig.canned_message.inputbroker_pin_a, interruptHandler, CHANGE);
    attachInterrupt(moduleConfig.canned_message.inputbroker_pin_b, interruptHandler, CHANGE);
    attachInterrupt(moduleConfig.canned_message.inputbroker_pin_press, interruptHandler, CHANGE);

    LOG_INFO("RotaryEncoder initialized pins(%d, %d, %d), events(%d, %d, %d)", moduleConfig.canned_message.inputbroker_pin_a,
             moduleConfig.canned_message.inputbroker_pin_b, moduleConfig.canned_message.inputbroker_pin_press, eventCw, eventCcw,
             eventPressed);
    return true;
}

void RotaryEncoderImpl::pollOnce()
{
    InputEvent e{ORIGIN_NAME, INPUT_BROKER_NONE, 0, 0, 0};

    static uint32_t lastPressed = millis();
    if (rotary->readButton() == RotaryEncoder::ButtonState::BUTTON_PRESSED) {
        if (lastPressed + 200 < millis()) {
            LOG_DEBUG("Rotary event Press");
            lastPressed = millis();
            e.inputEvent = this->eventPressed;
            inputBroker->queueInputEvent(&e);
        }
    }

    switch (rotary->process()) {
    case RotaryEncoder::DIRECTION_CW:
        LOG_DEBUG("Rotary event CW");
        e.inputEvent = this->eventCw;
        inputBroker->queueInputEvent(&e);
        break;
    case RotaryEncoder::DIRECTION_CCW:
        LOG_DEBUG("Rotary event CCW");
        e.inputEvent = this->eventCcw;
        inputBroker->queueInputEvent(&e);
        break;
    default:
        break;
    }
}

RotaryEncoderImpl *RotaryEncoderImpl::interruptInstance;

#endif