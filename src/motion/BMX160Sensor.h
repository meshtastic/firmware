#pragma once

#ifndef _BMX160_SENSOR_H_
#define _BMX160_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C

#if !defined(RAK2560) && __has_include(<Rak_BMX160.h>)

#include "Fusion/Fusion.h"
#include <Rak_BMX160.h>

// Numeric IDs for selecting which chip axis maps to world-up (vertical case mounts).
// Set BMX160_UP_AXIS to one of these via build flags (platformio.ini).
#define BMX160_UP_AXIS_PZ 0
#define BMX160_UP_AXIS_PX 1
#define BMX160_UP_AXIS_NX 2
#define BMX160_UP_AXIS_PY 3
#define BMX160_UP_AXIS_NY 4

class BMX160Sensor : public MotionSensor
{
  private:
    RAK_BMX160 sensor;
    bool showingScreen = false;
    static constexpr const char *compassCalibrationFileName = "/prefs/compass_bmx160.dat";
    float highestX = 0, lowestX = 0, highestY = 0, lowestY = 0, highestZ = 0, lowestZ = 0;

    // Per-axis EMA on raw accel + mag: the stateless compass fusion turns dynamic
    // acceleration into heading noise, so filtering the inputs steadies rotation.
    static constexpr float accelFilterAlpha = 0.15f;
    static constexpr float magFilterAlpha = 0.20f;
    FusionVector accelFiltered = {{0, 0, 0}};
    FusionVector magFiltered = {{0, 0, 0}};
    bool filtersSeeded = false;

  public:
    explicit BMX160Sensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;
    virtual void calibrate(uint16_t forSeconds) override;
    virtual bool providesHeading() const override { return true; }
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
