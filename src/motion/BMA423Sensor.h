#pragma once
#ifndef _BMA423_SENSOR_H_
#define _BMA423_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && defined(HAS_BMA423) && __has_include(<SensorBMA423.hpp>)

#include <SensorBMA423.hpp>
#include <Wire.h>

class BMA423Sensor : public MotionSensor
{
  private:
    SensorBMA423 sensor;
    volatile bool BMA_IRQ = false;

  public:
    explicit BMA423Sensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;
};

#endif

#endif