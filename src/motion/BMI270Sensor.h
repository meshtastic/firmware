#pragma once
#ifndef _BMI270_SENSOR_H_
#define _BMI270_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && defined(HAS_BMI270)

class BMI270Sensor : public MotionSensor
{
  private:
    bool initialized = false;
    TwoWire *wire = nullptr;

    // Previous readings for motion detection
    int16_t prevX = 0, prevY = 0, prevZ = 0;
    bool hasBaseline = false;

    // BMI270 register access
    bool writeRegister(uint8_t reg, uint8_t value);
    bool writeRegisters(uint8_t reg, const uint8_t *data, size_t len);
    uint8_t readRegister(uint8_t reg);
    bool readRegisters(uint8_t reg, uint8_t *data, size_t len);

    // Config file upload (BMI270 requires 8KB config blob)
    bool uploadConfigFile();

  public:
    explicit BMI270Sensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;
};

#endif

#endif
