#pragma once

#include <stdint.h>

/** Map desired total output to chip dBm and configure the external PA at the operating frequency in MHz. */
int8_t radioExternalPaMapPower(int8_t requestedTotalDbm, float frequencyMhz);
