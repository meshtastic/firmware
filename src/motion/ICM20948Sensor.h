#pragma once
#ifndef _ICM_20948_SENSOR_H_
#define _ICM_20948_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<ICM_20948.h>)

#include "Fusion/Fusion.h"
#include <ICM_20948.h>

// Set the default gyro scale - dps250, dps500, dps1000, dps2000
#ifndef ICM_20948_MPU_GYRO_SCALE
#define ICM_20948_MPU_GYRO_SCALE dps250
#endif

// Set the default accelerometer scale - gpm2, gpm4, gpm8, gpm16
#ifndef ICM_20948_MPU_ACCEL_SCALE
#define ICM_20948_MPU_ACCEL_SCALE gpm2
#endif

// Define a threshold for Wake on Motion Sensing (0mg to 1020mg)
#ifndef ICM_20948_WOM_THRESHOLD
#define ICM_20948_WOM_THRESHOLD 16U
#endif

// Define a pin in variant.h to use interrupts to read the ICM-20948
#ifndef ICM_20948_WOM_THRESHOLD
#define ICM_20948_INT_PIN 255
#endif

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

// The I2C address of the Accelerometer (if found) from main.cpp
extern ScanI2C::DeviceAddress accelerometer_found;

// Singleton wrapper for the Sparkfun ICM_20948_I2C class
class ICM20948Singleton : public ICM_20948_I2C
{
  private:
    static ICM20948Singleton *pinstance;

  protected:
    ICM20948Singleton();
    ~ICM20948Singleton();

  public:
    // Create a singleton instance (not thread safe)
    static ICM20948Singleton *GetInstance();

    // Singletons should not be cloneable.
    ICM20948Singleton(ICM20948Singleton &other) = delete;

    // Singletons should not be assignable.
    void operator=(const ICM20948Singleton &) = delete;

    // Initialise the motion sensor singleton for normal operation
    bool init(ScanI2C::FoundDevice device);

    // Enable Wake on Motion interrupts (sensor must be initialised first)
    bool setWakeOnMotion();

#ifdef ICM_20948_DMP_IS_ENABLED
    // Initialise the motion sensor singleton for digital motion processing
    bool initDMP();
#endif
};

class ICM20948Sensor : public MotionSensor
{
  private:
    ICM20948Singleton *sensor = nullptr;
    bool showingScreen = false;
    float highestX = 0, lowestX = 0, highestY = 0, lowestY = 0, highestZ = 0, lowestZ = 0;

  public:
    explicit ICM20948Sensor(ScanI2C::FoundDevice foundDevice);

    // Initialise the motion sensor
    virtual bool init() override;

    // Called each time our sensor gets a chance to run
    virtual int32_t runOnce() override;
    virtual void calibrate(uint16_t forSeconds) override;
};

#endif

#endif