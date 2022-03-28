#pragma once
#include "kbI2cBase.h"
#include "main.h"

/**
 * @brief The idea behind this class to have static methods for the event handlers.
 *      Check attachInterrupt() at RotaryEncoderInteruptBase.cpp
 *      Technically you can have as many rotary encoders hardver attached
 *      to your device as you wish, but you always need to have separate event
 *      handlers, thus you need to have a RotaryEncoderInterrupt implementation.
 */
class FacesKbI2cImpl :
    public KbI2cBase
{
  public:
    FacesKbI2cImpl();
    void init();
};

extern FacesKbI2cImpl *facesKbI2cImpl;