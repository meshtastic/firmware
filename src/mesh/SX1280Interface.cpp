#include "SX1280Interface.h"
#include "configuration.h"
#include "error.h"

SX1280Interface::SX1280Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                                 RADIOLIB_PIN_TYPE busy)
    : SX128xInterface(hal, cs, irq, rst, busy)
{
}
