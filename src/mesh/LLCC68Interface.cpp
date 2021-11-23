#include "configuration.h"
#include "LLCC68Interface.h"
#include "error.h"

LLCC68Interface::LLCC68Interface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy,
                                 SPIClass &spi)
    : SX126xInterface(cs, irq, rst, busy, spi)
{
}