#pragma once
#ifndef _MPU6050_SENSOR_H_
#define _MPU6050_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<Adafruit_MPU6050.h>)

#include <Adafruit_MPU6050.h>

class MPU6050Sensor : public MotionSensor
{
  private:
    Adafruit_MPU6050 sensor;

  public:
    explicit MPU6050Sensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;
};

#endif

#endif