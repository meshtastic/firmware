#pragma once
#include "kbI2cBase.h"

/**
 * @brief The idea behind this class to have static methods for the event handlers.
 *      Check attachInterrupt() at RotaryEncoderInteruptBase.cpp
 *      Technically you can have as many rotary encoders hardver attached
 *      to your device as you wish, but you always need to have separate event
 *      handlers, thus you need to have a RotaryEncoderInterrupt implementation.
 */
class CardKbI2cImpl : public KbI2cBase
{
  public:
    CardKbI2cImpl();
    void init();
};

extern CardKbI2cImpl *cardKbI2cImpl;