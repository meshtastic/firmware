#include "configuration.h"
#include "SX1268Interface.h"
#include "error.h"

SX1268Interface::SX1268Interface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy,
                                 SPIClass &spi)
    : SX126xInterface(cs, irq, rst, busy, spi)
{
}