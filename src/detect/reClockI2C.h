
#ifndef RECLOCK_I2C_
#define RECLOCK_I2C_

#include "ScanI2CTwoWire.h"
#include <stdint.h>

uint32_t reClockI2C(uint32_t desiredClock, TwoWire *i2cBus, bool force);

#endif
