#pragma once
#ifndef _QMC6310_SENSOR_H_
#define _QMC6310_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<SensorQMC6310.hpp>)

#include <SensorQMC6310.hpp>

class QMC6310Sensor : public MotionSensor
{
  private:
    SensorQMC6310 sensor;
    uint32_t lastLogMs = 0;

  public:
    explicit QMC6310Sensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;
};

#endif

#endif

