#include "configuration.h"

#ifdef MESHNOLOGY_W12

// The 2.4 GHz front end's LDO enable has no pull resistor fitted (R38 NC), so hold it low rather than
// leave it floating. The 2.4 GHz path is not supported yet.
void earlyInitVariant()
{
    pinMode(LORA_PA_EN_G, OUTPUT);
    digitalWrite(LORA_PA_EN_G, LOW);
}

#endif // MESHNOLOGY_W12
