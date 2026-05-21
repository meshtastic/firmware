#pragma once
#ifndef _LSM6DSO_SENSOR_H_
#define _LSM6DSO_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<Adafruit_LSM6DSOX.h>)

#ifndef LSM6DSO_WAKE_THRESH
#define LSM6DSO_WAKE_THRESH 20
#endif

#include <Adafruit_LSM6DSOX.h>

class LSM6DSOSensor : public MotionSensor
{
  private:
    Adafruit_LSM6DSOX sensor;

  public:
    explicit LSM6DSOSensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;
};

#endif

#endif
