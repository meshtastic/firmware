#include "RotaryEncoderInterruptImpl1.h"
#include "InputBroker.h"
extern bool osk_found;

RotaryEncoderInterruptImpl1 *rotaryEncoderInterruptImpl1;

RotaryEncoderInterruptImpl1::RotaryEncoderInterruptImpl1() : RotaryEncoderInterruptBase("rotEnc1") {}

bool RotaryEncoderInterruptImpl1::init()
{
    if (!moduleConfig.canned_message.rotary1_enabled) {
        // Input device is disabled.
        disable();
        return false;
    }

    uint8_t pinA = moduleConfig.canned_message.inputbroker_pin_a;
    uint8_t pinB = moduleConfig.canned_message.inputbroker_pin_b;
    uint8_t pinPress = moduleConfig.canned_message.inputbroker_pin_press;
    input_broker_event eventCw = static_cast<input_broker_event>(moduleConfig.canned_message.inputbroker_event_cw);
    input_broker_event eventCcw = static_cast<input_broker_event>(moduleConfig.canned_message.inputbroker_event_ccw);
    input_broker_event eventPressed = static_cast<input_broker_event>(moduleConfig.canned_message.inputbroker_event_press);
    input_broker_event eventPressedLong = INPUT_BROKER_SELECT_LONG;

    // moduleConfig.canned_message.ext_notification_module_output
    RotaryEncoderInterruptBase::init(pinA, pinB, pinPress, eventCw, eventCcw, eventPressed, eventPressedLong,
                                     RotaryEncoderInterruptImpl1::handleIntA, RotaryEncoderInterruptImpl1::handleIntB,
                                     RotaryEncoderInterruptImpl1::handleIntPressed);
    inputBroker->registerSource(this);
    osk_found = true;
    return true;
}

void RotaryEncoderInterruptImpl1::handleIntA()
{
    rotaryEncoderInterruptImpl1->intAHandler();
}
void RotaryEncoderInterruptImpl1::handleIntB()
{
    rotaryEncoderInterruptImpl1->intBHandler();
}
void RotaryEncoderInterruptImpl1::handleIntPressed()
{
    rotaryEncoderInterruptImpl1->intPressHandler();
}