#pragma once
#include "ButtonThread.h"
#include "main.h"

/**
 * @brief The idea behind this class to have static methods for the event handlers.
 *      Check attachInterrupt() at RotaryEncoderInteruptBase.cpp
 *      Technically you can have as many rotary encoders hardver attached
 *      to your device as you wish, but you always need to have separate event
 *      handlers, thus you need to have a RotaryEncoderInterrupt implementation.
 */
class ButtonThreadImpl : public ButtonThread
{
  public:
    ButtonThreadImpl(char *);
    void init(uint8_t pinNumber, bool activeLow, bool activePullup, uint32_t pullupSense, input_broker_event singlePress,
              input_broker_event longPress = INPUT_BROKER_NONE, input_broker_event doublePress = INPUT_BROKER_NONE,
              input_broker_event triplePress = INPUT_BROKER_NONE, input_broker_event shortLong = INPUT_BROKER_NONE,
              bool touchQuirk = false);
};