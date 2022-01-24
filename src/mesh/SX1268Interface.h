#pragma once

#include "SX126xInterface.h"

/**
 * Our adapter for SX1268 radios
 */
class SX1268Interface : public SX126xInterface<SX1268>
{
  public:
    /// override frequency of the SX1268 module regardless of the region (use EU433 value)
    virtual float getFreq() override { return 433.175f; }

    SX1268Interface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy, SPIClass &spi);
};
