#pragma once

#include "SX126xInterface.h"

/**
 * Our adapter for SX1262 radios
 */
class SX1262Interface : public SX126xInterface<SX1262>
{
  public:
    SX1262Interface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy, SPIClass &spi);
};