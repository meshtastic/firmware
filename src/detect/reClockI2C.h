
#ifndef RECLOCK_I2C_
#define RECLOCK_I2C_

#include "../graphics/Screen.h"
#include "ScanI2CTwoWire.h"
#include <Wire.h>
#include <stdint.h>

uint32_t reClockI2C(uint32_t desiredClock, TwoWire *i2cBus, ScanI2C::I2CPort port);

extern graphics::Screen *screen;

#endif
