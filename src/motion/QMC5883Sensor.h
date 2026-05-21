#pragma once
#ifndef _QMC5883_SENSOR_H_
#define _QMC5883_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<Adafruit_QMC5883P.h>)

#include <Adafruit_QMC5883P.h>
#include <Wire.h>

// The I2C address of the Accelerometer (if found) from main.cpp
extern ScanI2C::DeviceAddress accelerometer_found;

class QMC5883Sensor : public MotionSensor
{
  private:
    Adafruit_QMC5883P sensor;
    bool showingScreen = false;
    float highestX = 0, lowestX = 0, highestY = 0, lowestY = 0, highestZ = 0, lowestZ = 0;
    float magOffsetX = 0, magOffsetY = 0, magOffsetZ = 0;

    void loadCalibration();
    void saveCalibration();

  public:
    explicit QMC5883Sensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;
    virtual void calibrate(uint16_t forSeconds) override;
};

#endif
#endif
