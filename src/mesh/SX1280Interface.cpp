#include "SX1280Interface.h"
#include "configuration.h"
#include "error.h"

SX1280Interface::SX1280Interface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy,
                                 SPIClass &spi)
    : SX128xInterface(cs, irq, rst, busy, spi)
{
}
