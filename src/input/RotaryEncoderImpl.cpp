#include "configuration.h"
#if defined(FSM_ROTARY_ENCODER)

#include "RotaryEncoderImpl.h"
#include "InputBroker.h"
#include "RotaryEncoder.h"
extern bool osk_found;

#define ORIGIN_NAME "RotaryEncoder"

#ifndef FSM_ROTARY_ENCODER_DEBOUNCE
#define FSM_ROTARY_ENCODER_DEBOUNCE 200
#endif

#ifndef FSM_ROTARY_ENCODER_CHECK_INTERVAL
#define FSM_ROTARY_ENCODER_CHECK_INTERVAL 10
#endif

RotaryEncoderImpl *rotaryEncoderImpl;

RotaryEncoderImpl::RotaryEncoderImpl() : concurrency::OSThread(ORIGIN_NAME), originName(ORIGIN_NAME)
{
    rotary = nullptr;
}

bool RotaryEncoderImpl::init()
{
    if (!moduleConfig.canned_message.updown1_enabled || moduleConfig.canned_message.inputbroker_pin_a == 0 ||
        moduleConfig.canned_message.inputbroker_pin_b == 0) {
        // Input device is disabled.
        disable();
        return false;
    }

    eventCw = static_cast<input_broker_event>(moduleConfig.canned_message.inputbroker_event_cw);
    eventCcw = static_cast<input_broker_event>(moduleConfig.canned_message.inputbroker_event_ccw);
    eventPressed = static_cast<input_broker_event>(moduleConfig.canned_message.inputbroker_event_press);

    rotary = new RotaryEncoder(moduleConfig.canned_message.inputbroker_pin_a, moduleConfig.canned_message.inputbroker_pin_b,
                               moduleConfig.canned_message.inputbroker_pin_press);
    rotary->resetButton();

    inputBroker->registerSource(this);

    LOG_INFO("RotaryEncoder initialized pins(%d, %d, %d), events(%d, %d, %d)", moduleConfig.canned_message.inputbroker_pin_a,
             moduleConfig.canned_message.inputbroker_pin_b, moduleConfig.canned_message.inputbroker_pin_press, eventCw, eventCcw,
             eventPressed);
    osk_found = true;
    return true;
}

int32_t RotaryEncoderImpl::runOnce()
{
    InputEvent e{originName, INPUT_BROKER_NONE, 0, 0, 0};
    static uint32_t lastPressed = millis();
    if (rotary->readButton() == RotaryEncoder::ButtonState::BUTTON_PRESSED) {
        if (lastPressed + FSM_ROTARY_ENCODER_DEBOUNCE < millis()) {
            LOG_DEBUG("Rotary event Press");
            lastPressed = millis();
            e.inputEvent = this->eventPressed;
        }
    } else {
        switch (rotary->process()) {
        case RotaryEncoder::DIRECTION_CW:
            LOG_DEBUG("Rotary event CW");
            e.inputEvent = this->eventCw;
            break;
        case RotaryEncoder::DIRECTION_CCW:
            LOG_DEBUG("Rotary event CCW");
            e.inputEvent = this->eventCcw;
            break;
        default:
            break;
        }
    }

    if (e.inputEvent != INPUT_BROKER_NONE) {
        this->notifyObservers(&e);
    }

    return FSM_ROTARY_ENCODER_CHECK_INTERVAL;
}

#endif