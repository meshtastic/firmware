#pragma once

#include "SX126xInterface.h"

/**
 * Our adapter for LLCC68 radios
 * https://www.semtech.com/products/wireless-rf/lora-core/llcc68
 */
class LLCC68Interface : public SX126xInterface<LLCC68>
{
  public:
    LLCC68Interface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy, SPIClass &spi);
};