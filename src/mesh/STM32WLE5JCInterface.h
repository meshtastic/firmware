#pragma once

#ifdef ARCH_STM32WL
#include "SX126xInterface.h"
#include "rfswitch.h"

/**
 * Our adapter for STM32WLE5JC radios
 */
class STM32WLE5JCInterface : public SX126xInterface
{
  public:
    STM32WLE5JCInterface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                         RADIOLIB_PIN_TYPE busy);

    virtual bool init() override;

  protected:
    STM32WLx chip;

    int16_t beginChip(bool useRegulatorLDO) override
    {
        return chip.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage, useRegulatorLDO);
    }

#ifdef SX126X_NO_POWER_OPTIMIZATION_TABLE
    int16_t setOutputPowerFixedPa(int8_t txPower) override { return chip.setOutputPower(txPower, false); }
#endif
};

#endif // ARCH_STM32WL