#pragma once

#ifndef _BMX160_SENSOR_H_
#define _BMX160_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C

#if defined(RAK_4631) && !defined(RAK2560)

#include "Fusion/Fusion.h"
#include <Rak_BMX160.h>

class BMX160Sensor : public MotionSensor
{
  private:
    RAK_BMX160 sensor;

  public:
    explicit BMX160Sensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;
    virtual void calibrate(uint16_t forSeconds) override;
};

#else

// Stub
class BMX160Sensor : public MotionSensor
{
  public:
    explicit BMX160Sensor(ScanI2C::FoundDevice foundDevice);
};

#endif

#endif

#endif