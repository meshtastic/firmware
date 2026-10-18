#if RADIOLIB_EXCLUDE_SX126X != 1
#pragma once

#include "SX126xInterface.h"

/**
 * Our adapter for SX1262 radios
 */
class SX1262Interface : public SX126xInterface
{
  public:
    SX1262Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                    RADIOLIB_PIN_TYPE busy);

  protected:
    SX1262 chip;

    int16_t beginChip(bool useRegulatorLDO) override
    {
        return chip.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage, useRegulatorLDO);
    }

#ifdef SX126X_NO_POWER_OPTIMIZATION_TABLE
    int16_t setOutputPowerFixedPa(int8_t txPower) override { return chip.setOutputPower(txPower, false); }
#endif
};
#endif