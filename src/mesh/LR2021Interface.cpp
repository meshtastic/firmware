#include "configuration.h"

#if defined(USE_LR2021) && RADIOLIB_EXCLUDE_LR2021 != 1

#include "LR2021Interface.h"
#include "error.h"

LR2021Interface::LR2021Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                                 RADIOLIB_PIN_TYPE busy)
    : LR20x0Interface(hal, cs, irq, rst, busy)
{
}

bool LR2021Interface::wideLora()
{
    return true;
}
#endif
