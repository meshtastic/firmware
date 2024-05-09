#pragma once

#include "LR11x0Interface.h"

/**
 * Our adapter for LR1120 wideband radios
 */
class LR1120Interface : public LR11x0Interface<LR1120>
{
  public:
    LR1120Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                    RADIOLIB_PIN_TYPE busy);
};