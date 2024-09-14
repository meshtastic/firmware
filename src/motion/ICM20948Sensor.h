#pragma once
#ifndef _ICM_20948_SENSOR_H_
#define _ICM_20948_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include <ICM_20948.h>

// Set the default sensor scales
#define ICM_20948_MPU_GYRO_SCALE dps500 // dps250, dps500, dps1000, dps2000
#define ICM_20948_MPU_ACCEL_SCALE gpm2  // gpm2, gpm4, gpm8, gpm16

// Define a pin in variant.h to use interrupts to read the ICM-20948
// #define ICM_20948_INT_PIN 5

// Define a threshold for Wake on Motion Sensing (0mg to 1020mg)
#define ICM_20948_WOM_THRESHOLD 16U

// Uncomment this line to enable helpful debug messages on Serial
// #define ICM_20948_DEBUG 1

// Uncomment this line to enable the onboard digital motion processor (to be added in a future PR)
// #define ICM_20948_DMP_IS_ENABLED 1

// Check for a mandatory compiler flag to use the DMP (to be added in a future PR)
#ifdef ICM_20948_DMP_IS_ENABLED
#ifndef ICM_20948_USE_DMP
#error To use the digital motion processor, please either set the compiler flag ICM_20948_USE_DMP or uncomment line 29 (#define ICM_20948_USE_DMP) in ICM_20948_C.h
#endif
#endif

class ICM20948Sensor : public MotionSensor
{
  private:
    ICM_20948_I2C sensor;

  protected:
    virtual bool initSensor();

  public:
    ICM20948Sensor(ScanI2C::DeviceAddress address);

    // Initialise the motion sensor
    virtual bool init() override;

    // Called each time our sensor gets a chance to run
    virtual int32_t runOnce() override;
};

#endif

#endif