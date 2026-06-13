#pragma once

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_I2C && defined(TRACKER_T1000_E) && (defined(ARCH_NRF52) || defined(NRF52_SERIES) || defined(NRF52))

#include <stdint.h>

namespace T1000EI2C
{
void restoreBus();
bool readRegister(uint8_t address, uint8_t reg, uint8_t &value);
bool writeRegister(uint8_t address, uint8_t reg, uint8_t value);
} // namespace T1000EI2C

#endif
