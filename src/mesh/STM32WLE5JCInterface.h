#pragma once

#ifdef ARCH_STM32WL
#include "SX126xInterface.h"
#include "rfswitch.h"

/**
 * Our adapter for STM32WLE5JC radios
 */
class STM32WLE5JCInterface : public SX126xInterface<STM32WLx>
{
  public:
    STM32WLE5JCInterface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                         RADIOLIB_PIN_TYPE busy);

    virtual bool init() override;
};

#endif // ARCH_STM32WL