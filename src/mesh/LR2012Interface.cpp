#include "configuration.h"

#if (defined(USE_LR2012) || defined(ARCH_PORTDUINO)) && RADIOLIB_EXCLUDE_LR2021 != 1

#include "LR2012Interface.h"
#include "error.h"

LR2012Interface::LR2012Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                                 RADIOLIB_PIN_TYPE busy)
    : LR20x0Interface(hal, cs, irq, rst, busy)
{
}

bool LR2012Interface::wideLora()
{
    // sub-GHz only: no HF PA, LNA or mixer, so LORA_24 is out of reach
    return false;
}
#endif
