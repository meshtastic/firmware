#pragma once
#ifndef _LSM6DS3_SENSOR_H_
#define _LSM6DS3_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C

#ifndef LSM6DS3_WAKE_THRESH
#define LSM6DS3_WAKE_THRESH 20
#endif

#include <Adafruit_LSM6DS3TRC.h>

class LSM6DS3Sensor : public MotionSensor
{
  private:
    Adafruit_LSM6DS3TRC sensor;

  public:
    explicit LSM6DS3Sensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;
};

#endif

#endif