#pragma once

#include "SX128xInterface.h"

/**
 * Our adapter for SX1281 radios
 */

#if !defined(ARCH_PORTDUINO)

class SX1281Interface : public SX128xInterface<SX1281>
{
  public:
    SX1281Interface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy, SPIClass &spi);
};

#endif