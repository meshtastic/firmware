#include "configuration.h"

#ifdef _VARIANT_nm_epd_420

void earlyInitVariant()
{
    pinMode(PIN_AMP_ENABLE, OUTPUT);
    digitalWrite(PIN_AMP_ENABLE, LOW);

    pinMode(PIN_ES8311_POWER, OUTPUT);
    digitalWrite(PIN_ES8311_POWER, LOW);

    pinMode(AHTX0_POWER_PIN, OUTPUT);
    digitalWrite(AHTX0_POWER_PIN, HIGH);
}

void lateInitVariant()
{
    digitalWrite(AHTX0_POWER_PIN, LOW);
}

#endif
