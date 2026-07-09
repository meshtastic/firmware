#include "RadioExternalPa.h"

// Weak default implementations: no external PA. A board with an external PA
// provides strong overrides (see e.g.
// src/platform/extra_variants/radiomaster_nomad_gemini/variant.cpp).

int8_t __attribute__((weak)) radioExternalPaMapPower(int8_t requestedTotalDbm, float freqHz)
{
    (void)freqHz;
    (void)requestedTotalDbm;
    return RADIO_EXTERNAL_PA_NO_MAP;
}

void __attribute__((weak)) radioExternalPaTxEnable() {}

void __attribute__((weak)) radioExternalPaRxIdle() {}

void __attribute__((weak)) radioExternalPaSleep() {}
