#pragma once

#include "MotionSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && !MESHTASTIC_EXCLUDE_ACCELEROMETER && __has_include(<Adafruit_LSM6DSOX.h>)

#include <Adafruit_LSM6DSOX.h>

#ifndef LSM6DSOX_WAKE_THRESH
#define LSM6DSOX_WAKE_THRESH 20
#endif

class LSM6DSOXSensor : public MotionSensor
{
  private:
    class Sensor : public Adafruit_LSM6DSOX
    {
      public:
        bool configureWakeup(bool enable);
    } sensor;

  public:
    explicit LSM6DSOXSensor(ScanI2C::FoundDevice foundDevice);
    bool init() override;
    int32_t runOnce() override;
};

#endif