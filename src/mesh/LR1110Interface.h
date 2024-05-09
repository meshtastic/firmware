#pragma once

#include "LR11x0Interface.h"

/**
 * Our adapter for LR1110 radios
 */
class LR1110Interface : public LR11x0Interface<LR1110>
{
  public:
    LR1110Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                    RADIOLIB_PIN_TYPE busy);
};