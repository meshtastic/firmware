#include "RotaryEncoderInterruptImpl1.h"

RotaryEncoderInterruptImpl1 *rotaryEncoderInterruptImpl1;

RotaryEncoderInterruptImpl1::RotaryEncoderInterruptImpl1(
    uint8_t pinA, uint8_t pinB, uint8_t pinPress,
    char eventCw, char eventCcw, char eventPressed) :
    RotaryEncoderInterruptBase(
        "rotEnc1",
        pinA, pinB, pinPress,
        eventCw, eventCcw, eventPressed,
        RotaryEncoderInterruptImpl1::handleIntA,
        RotaryEncoderInterruptImpl1::handleIntB,
        RotaryEncoderInterruptImpl1::handleIntPressed)
{

}

void RotaryEncoderInterruptImpl1::handleIntA()
{

}
void RotaryEncoderInterruptImpl1::handleIntB()
{

}
void RotaryEncoderInterruptImpl1::handleIntPressed()
{

}
