#pragma once
#ifndef _LIS3DH_SENSOR_H_
#define _LIS3DH_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<Adafruit_LIS3DH.h>)

#include <Adafruit_LIS3DH.h>

class LIS3DHSensor : public MotionSensor
{
  private:
    Adafruit_LIS3DH sensor;

  public:
    explicit LIS3DHSensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;
};

#endif

#endif