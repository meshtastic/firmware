#pragma once
#ifndef _IIS2MDCTR_SENSOR_H_
#define _IIS2MDCTR_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C

// Standalone magnetometer register driver (no vendor library exists for this chip in this
// firmware). Commonly wired alongside an ISM330DHCX on ST "fusion sensor hub" combo breakouts;
// tilt-compensates its heading using the accel sample published by ISM330DHCXSensor, mirroring
// the QMC6309/MMC5983MA + QMI8658/ICM42607P pairing.
class IIS2MDCTRSensor : public MotionSensor
{
  private:
    bool showingScreen = false;
    static constexpr const char *compassCalibrationFileName = "/prefs/compass_iis2mdctr.dat";
    float highestX = 0, lowestX = 0, highestY = 0, lowestY = 0, highestZ = 0, lowestZ = 0;

    bool writeRegister(uint8_t reg, uint8_t value);
    bool readRegisters(uint8_t reg, uint8_t *buffer, size_t count);
    bool readMagnetometer(float &xGauss, float &yGauss, float &zGauss);

  public:
    explicit IIS2MDCTRSensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;
    virtual void calibrate(uint16_t forSeconds) override;
};

#endif

#endif
