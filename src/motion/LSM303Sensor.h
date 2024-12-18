#pragma once
#ifndef _LSM303_SENSOR_H_
#define _LSM303_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C
#include <Adafruit_Sensor.h>
#include <Adafruit_LSM303_Accel.h>

class LSM303Sensor : public MotionSensor
{
  private:
    Adafruit_LSM303_Accel_Unified sensor;

  public:
    explicit LSM303Sensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;
};

#endif

#endif