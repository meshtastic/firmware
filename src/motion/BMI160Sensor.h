#pragma once
#ifndef _BMI160_SENSOR_H_
#define _BMI160_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && defined(HAS_BMI160)

/**
 * Driver for the Bosch BMI160 6-axis IMU, used in accelerometer-only mode.
 *
 * The BMI160 shares its I2C addresses (0x68/0x69) with the MPU6050, BMI270,
 * BMX160 and ICM-42607-P, and is told apart by the fixed chip ID 0xD1 in
 * register 0x00.
 *
 * Unlike the BMI270 it needs no firmware blob uploaded on init: writing the
 * accelerometer range/bandwidth, enabling the accelerometer, and issuing the
 * normal-mode command is enough to start producing samples. Register access is
 * done directly over I2C, so no external library is required.
 *
 * Only the accelerometer is used. The gyroscope is left in suspend to save
 * power, and there is no magnetometer, so this sensor never provides a compass
 * heading.
 */
class BMI160Sensor : public MotionSensor
{
  private:
    bool initialized = false;
    TwoWire *wire = nullptr;

    // Previous sample, used as the baseline for motion detection
    int16_t prevX = 0, prevY = 0, prevZ = 0;
    bool hasBaseline = false;

    bool writeRegister(uint8_t reg, uint8_t value);
    uint8_t readRegister(uint8_t reg);
    bool readRegisters(uint8_t reg, uint8_t *data, size_t len);

  public:
    explicit BMI160Sensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;
};

#endif

#endif
