#include "LR1110Interface.h"
#include "configuration.h"
#include "error.h"

LR1110Interface::LR1110Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                                 RADIOLIB_PIN_TYPE busy)
    : LR11x0Interface(hal, cs, irq, rst, busy)
{
}