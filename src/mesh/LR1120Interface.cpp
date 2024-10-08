#if RADIOLIB_EXCLUDE_LR11X0 != 1

#include "LR1120Interface.h"
#include "configuration.h"
#include "error.h"

LR1120Interface::LR1120Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                                 RADIOLIB_PIN_TYPE busy)
    : LR11x0Interface(hal, cs, irq, rst, busy)
{
}

bool LR1120Interface::wideLora()
{
    return true;
}
#endif