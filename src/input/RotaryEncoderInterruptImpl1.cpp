#include "RotaryEncoderInterruptImpl1.h"
#include "InputBroker.h"

RotaryEncoderInterruptImpl1 *rotaryEncoderInterruptImpl1;

RotaryEncoderInterruptImpl1::RotaryEncoderInterruptImpl1() :
    RotaryEncoderInterruptBase(
        "rotEnc1")
{
}

void RotaryEncoderInterruptImpl1::init()
{
    if (!radioConfig.preferences.rotary1_enabled)
    {
        // Input device is disabled.
        return;
    }

    uint8_t pinA = radioConfig.preferences.rotary1_pin_a;
    uint8_t pinB = radioConfig.preferences.rotary1_pin_b;
    uint8_t pinPress = radioConfig.preferences.rotary1_pin_press;
    char eventCw =
        static_cast<char>(radioConfig.preferences.rotary1_event_cw);
    char eventCcw =
        static_cast<char>(radioConfig.preferences.rotary1_event_ccw);
    char eventPressed =
        static_cast<char>(radioConfig.preferences.rotary1_event_press);

    //radioConfig.preferences.ext_notification_module_output
    RotaryEncoderInterruptBase::init(
        pinA, pinB, pinPress,
        eventCw, eventCcw, eventPressed,
        RotaryEncoderInterruptImpl1::handleIntA,
        RotaryEncoderInterruptImpl1::handleIntB,
        RotaryEncoderInterruptImpl1::handleIntPressed);
    inputBroker->registerSource(this);
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
