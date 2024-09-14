#pragma once

#ifndef _BMX160_SENSOR_H_
#define _BMX160_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#ifdef RAK_4631

#include "Fusion/Fusion.h"
#include <Rak_BMX160.h>

class BMX160Sensor : public MotionSensor
{
  private:
    RAK_BMX160 sensor;
    bool showingScreen = false;
    float highestX = 0, lowestX = 0, highestY = 0, lowestY = 0, highestZ = 0, lowestZ = 0;

  public:
    BMX160Sensor(ScanI2C::DeviceAddress address);
    virtual bool init() override;
    virtual int32_t runOnce() override;
};

#else

// Stub
class BMX160Sensor : public MotionSensor
{
  public:
    BMX160Sensor(ScanI2C::DeviceAddress address);
};

#endif

#endif

#endif