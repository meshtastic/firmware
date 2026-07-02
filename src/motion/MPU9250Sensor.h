#pragma once
#ifndef _MPU9250_SENSOR_H_
#define _MPU9250_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C

#include "Fusion/Fusion.h"
#include <Wire.h>

// Numeric IDs for selecting which chip axis maps to world-up.
// Set MPU9250_UP_AXIS to one of these in variant.h or via build flags.
#define MPU9250_UP_AXIS_PZ 0
#define MPU9250_UP_AXIS_PX 1
#define MPU9250_UP_AXIS_NX 2
#define MPU9250_UP_AXIS_PY 3
#define MPU9250_UP_AXIS_NY 4

// InvenSense MPU-9250/MPU-9255 9-axis IMU driver (e.g. RAK1905 WisBlock).
// Drives the MPU-6500 accel die at deviceAddress() and the AK8963 magnetometer
// at AK8963_ADDR over I2C bypass mode.
class MPU9250Sensor : public MotionSensor
{
  private:
    TwoWire *bus = nullptr;
    bool showingScreen = false;

    // Per-axis AK8963 factory sensitivity adjustment scale factors derived from
    // the chip's Fuse ROM (ASA registers). Applied to every raw mag sample.
    float asaScale[3] = {1.0f, 1.0f, 1.0f};

    // Hard-iron calibration extrema persisted to flash.
    static constexpr const char *compassCalibrationFileName = "/prefs/compass_mpu9250.dat";
    float highestX = 0, lowestX = 0, highestY = 0, lowestY = 0, highestZ = 0, lowestZ = 0;

    // Per-axis EMA on raw accel + mag: the stateless compass fusion turns dynamic
    // acceleration into heading noise, so filtering the inputs steadies rotation.
    static constexpr float accelFilterAlpha = 0.15f;
    static constexpr float magFilterAlpha = 0.20f;
    FusionVector accelFiltered = {{0, 0, 0}};
    FusionVector magFiltered = {{0, 0, 0}};
    bool filtersSeeded = false;

    // Pick the correct TwoWire instance for the detected device.
    TwoWire *resolveBus() const;

    // Low-level I2C helpers - one address per call so we can address both dies.
    bool writeRegister(uint8_t i2cAddr, uint8_t reg, uint8_t value);
    bool readRegisters(uint8_t i2cAddr, uint8_t reg, uint8_t *buf, uint8_t len);

    // Init helpers.
    bool initMPU6500();
    bool initAK8963();

    // Read accel (g) and mag (µT) into Fusion vectors; returns false if mag
    // had no fresh sample on this tick.
    bool readSensors(FusionVector &accel, FusionVector &mag);

  public:
    explicit MPU9250Sensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;
    virtual void calibrate(uint16_t forSeconds) override;
};

#endif

#endif
