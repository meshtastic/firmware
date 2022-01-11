#include "RotaryEncoderInterruptImpl1.h"
#include "InputBroker.h"

RotaryEncoderInterruptImpl1 *rotaryEncoderInterruptImpl1;

RotaryEncoderInterruptImpl1::RotaryEncoderInterruptImpl1() :
    RotaryEncoderInterruptBase(
        "rotEnc1")
{
}

void RotaryEncoderInterruptImpl1::init(
    uint8_t pinA, uint8_t pinB, uint8_t pinPress,
    char eventCw, char eventCcw, char eventPressed)
{
    RotaryEncoderInterruptBase::init(
        pinA, pinB, pinPress,
        eventCw, eventCcw, eventPressed,
        RotaryEncoderInterruptImpl1::handleIntA,
        RotaryEncoderInterruptImpl1::handleIntB,
        RotaryEncoderInterruptImpl1::handleIntPressed);
    inputBroker->registerOrigin(this);
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
