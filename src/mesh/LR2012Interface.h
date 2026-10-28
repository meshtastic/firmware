#pragma once
#if RADIOLIB_EXCLUDE_LR2021 != 1
#include "LR20x0Interface.h"

/**
 * Our adapter for LR2012 radios. Same silicon as the LR2021 minus the high-frequency front end,
 * so it shares LR20x0Interface and differs only in having no 2.4 GHz band.
 */
class LR2012Interface : public LR20x0Interface<LR2012>
{
  public:
    LR2012Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                    RADIOLIB_PIN_TYPE busy);
    bool wideLora() override;
};
#endif
