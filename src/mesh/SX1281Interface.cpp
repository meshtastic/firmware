#include "configuration.h"
#include "SX1281Interface.h"
#include "error.h"

#if !defined(ARCH_PORTDUINO)

SX1281Interface::SX1281Interface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, RADIOLIB_PIN_TYPE busy,
                                 SPIClass &spi)
    : SX128xInterface(cs, irq, rst, busy, spi)
{
}

#endif