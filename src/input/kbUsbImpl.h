#pragma once
#include "kbUsbBase.h"
#include "main.h"

#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2

/**
 * @brief The idea behind this class to have static methods for the event handlers.
 *      Check attachInterrupt() at RotaryEncoderInteruptBase.cpp
 *      Technically you can have as many rotary encoders hardver attached
 *      to your device as you wish, but you always need to have separate event
 *      handlers, thus you need to have a RotaryEncoderInterrupt implementation.
 */
class KbUsbImpl : public KbUsbBase
{
  public:
    KbUsbImpl();
    void init();
};

extern KbUsbImpl *kbUsbImpl;

#endif