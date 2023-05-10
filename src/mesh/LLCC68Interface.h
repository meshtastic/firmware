#pragma once

#include "SX126xInterface.h"

/**
 * Our adapter for LLCC68 radios
 * https://www.semtech.com/products/wireless-rf/lora-core/llcc68
 * ⚠️⚠️⚠️
 * Be aware that LLCC68 does not support Spreading Factor 12 (SF12) and will not work on the "LongSlow" and "VLongSlow" channels.
 * You must change the channel if you get `Critical Error #3` with this module.
 * ⚠️⚠️⚠️
 */
class LLCC68Interface : public SX126xInterface<LLCC68>
{
  public:
    LLCC68Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                    RADIOLIB_PIN_TYPE busy);
};