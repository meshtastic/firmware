#pragma once

#include "configuration.h"

// Only the QMA6100P probing path (T1000-E) uses this TWIM helper; gate it on
// HAS_QMA6100P so it isn't compiled/analyzed as dead code on other nRF52 boards.
#if defined(ARCH_NRF52) && defined(HAS_QMA6100P)

#include <stdint.h>

namespace Nrf52Twim
{
void restoreBus();
bool readRegister(uint8_t address, uint8_t reg, uint8_t &value);
bool writeRegister(uint8_t address, uint8_t reg, uint8_t value);
} // namespace Nrf52Twim

#endif
