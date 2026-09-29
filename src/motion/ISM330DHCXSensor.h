#pragma once
#ifndef _ISM330DHCX_SENSOR_H_
#define _ISM330DHCX_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<Adafruit_ISM330DHCX.h>)

#ifndef ISM330DHCX_WAKE_THRESH
#define ISM330DHCX_WAKE_THRESH 1
#endif

#include <Adafruit_ISM330DHCX.h>

// Accel+gyro half of the ST "fusion sensor hub" combo. Handles both wiring styles a companion
// magnetometer (typically IIS2MDCTR) can use:
//  - Shared main bus: the magnetometer has its own I2C address, is detected independently by
//    ScanI2C, and is driven by IIS2MDCTRSensor on the separate MagnetometerThread. This driver
//    only publishes accel samples for that path to fuse against.
//  - ISM330DHCX aux sensor-hub bus: the magnetometer is wired to the IMU's internal SDx/SCx pins
//    only and is invisible to the main I2C scan. If no standalone magnetometer was found on the
//    main bus, this driver configures the IMU's embedded Sensor Hub (I2C master) to read it
//    directly and publishes both accel and mag samples itself.
class ISM330DHCXSensor : public MotionSensor
{
  private:
    Adafruit_ISM330DHCX sensor;
    bool auxMagAvailable = false;
    bool showingScreen = false;
    static constexpr const char *auxCompassCalibrationFileName = "/prefs/compass_ism330dhcx_aux.dat";
    float highestX = 0, lowestX = 0, highestY = 0, lowestY = 0, highestZ = 0, lowestZ = 0;

    bool writeRegister(uint8_t reg, uint8_t value);
    bool readRegisters(uint8_t reg, uint8_t *buffer, size_t count);
    bool setMemBank(uint8_t bank);
    bool configureAuxMagnetometer();
    bool readAuxMagnetometer(float &xGauss, float &yGauss, float &zGauss);

  public:
    explicit ISM330DHCXSensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;
    virtual void calibrate(uint16_t forSeconds) override;
    // for when using the ISM330DHCX as a sensor hub connected to a magnetometer
    virtual bool providesHeading() const override { return auxMagAvailable; }
};

#endif

#endif
