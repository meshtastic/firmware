#pragma once
#if RADIOLIB_EXCLUDE_LR2021 != 1
#include "LR20x0Interface.h"

/**
 * Our adapter for LR2021 radios
 */
class LR2021Interface : public LR20x0Interface<LR2021>
{
  public:
    LR2021Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                    RADIOLIB_PIN_TYPE busy);

};
#endif
