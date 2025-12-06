#pragma once
#if RADIOLIB_EXCLUDE_SX126X != 1

#include "SX126xInterface.h"

/**
 * Our adapter for SX1268 radios
 */
class SX1268Interface : public SX126xInterface<SX1268>
{
  public:
    virtual float getFreq() override;

    SX1268Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                    RADIOLIB_PIN_TYPE busy);
};
#endif