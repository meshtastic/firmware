#pragma once
#include "RotaryEncoderInterruptBase.h"

class RotaryEncoderInterruptImpl1 :
    public RotaryEncoderInterruptBase
{
  public:
    RotaryEncoderInterruptImpl1(
        uint8_t pinA, uint8_t pinB, uint8_t pinPress,
        char eventCw, char eventCcw, char eventPressed);
    static void handleIntA();
    static void handleIntB();
    static void handleIntPressed();
};

extern RotaryEncoderInterruptImpl1 *rotaryEncoderInterruptImpl1;