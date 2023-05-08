#pragma once

#include "SX128xInterface.h"

/**
 * Our adapter for SX1280 radios
 */

class SX1280Interface : public SX128xInterface<SX1280>
{
  public:
    SX1280Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                    RADIOLIB_PIN_TYPE busy);
};
