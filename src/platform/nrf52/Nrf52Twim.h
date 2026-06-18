#pragma once

#include "configuration.h"

#ifdef ARCH_NRF52

#include <stdint.h>

namespace Nrf52Twim
{
void restoreBus();
bool readRegister(uint8_t address, uint8_t reg, uint8_t &value);
bool writeRegister(uint8_t address, uint8_t reg, uint8_t value);
} // namespace Nrf52Twim

#endif
