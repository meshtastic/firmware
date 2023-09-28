#include "RotaryEncoderInterruptImpl1.h"
#include "InputBroker.h"

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
    char eventCw = static_cast<char>(moduleConfig.canned_message.inputbroker_event_cw);
    char eventCcw = static_cast<char>(moduleConfig.canned_message.inputbroker_event_ccw);
    char eventPressed = static_cast<char>(moduleConfig.canned_message.inputbroker_event_press);

    // moduleConfig.canned_message.ext_notification_module_output
    RotaryEncoderInterruptBase::init(pinA, pinB, pinPress, eventCw, eventCcw, eventPressed,
                                     RotaryEncoderInterruptImpl1::handleIntA, RotaryEncoderInterruptImpl1::handleIntB,
                                     RotaryEncoderInterruptImpl1::handleIntPressed);
    inputBroker->registerSource(this);
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