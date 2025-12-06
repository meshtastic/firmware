#pragma once
#include "RotaryEncoderInterruptBase.h"

/**
 * @brief The idea behind this class to have static methods for the event handlers.
 *      Check attachInterrupt() at RotaryEncoderInteruptBase.cpp
 *      Technically you can have as many rotary encoders hardver attached
 *      to your device as you wish, but you always need to have separate event
 *      handlers, thus you need to have a RotaryEncoderInterrupt implementation.
 */
class RotaryEncoderInterruptImpl1 : public RotaryEncoderInterruptBase
{
  public:
    RotaryEncoderInterruptImpl1();
    bool init();
    static void handleIntA();
    static void handleIntB();
    static void handleIntPressed();
};

extern RotaryEncoderInterruptImpl1 *rotaryEncoderInterruptImpl1;