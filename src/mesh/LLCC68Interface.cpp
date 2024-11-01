#if RADIOLIB_EXCLUDE_SX126X != 1
#include "LLCC68Interface.h"
#include "configuration.h"
#include "error.h"

LLCC68Interface::LLCC68Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                                 RADIOLIB_PIN_TYPE busy)
    : SX126xInterface(hal, cs, irq, rst, busy)
{
}
#endif