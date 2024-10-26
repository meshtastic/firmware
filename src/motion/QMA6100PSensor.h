#pragma once
#ifndef _QMA_6100P_SENSOR_H_
#define _QMA_6100P_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && defined(HAS_QMA6100P)

#include <QMA6100P.h>

// Set the default accelerometer scale - gpm2, gpm4, gpm8, gpm16
#ifndef QMA_6100P_MPU_ACCEL_SCALE
#define QMA_6100P_MPU_ACCEL_SCALE SFE_QMA6100P_RANGE32G
#endif

// The I2C address of the Accelerometer (if found) from main.cpp
extern ScanI2C::DeviceAddress accelerometer_found;

// Singleton wrapper for the Sparkfun QMA_6100P_I2C class
class QMA6100PSingleton : public QMA6100P
{
  private:
    static QMA6100PSingleton *pinstance;

  protected:
    QMA6100PSingleton();
    ~QMA6100PSingleton();

  public:
    // Create a singleton instance (not thread safe)
    static QMA6100PSingleton *GetInstance();

    // Singletons should not be cloneable.
    QMA6100PSingleton(QMA6100PSingleton &other) = delete;

    // Singletons should not be assignable.
    void operator=(const QMA6100PSingleton &) = delete;

    // Initialise the motion sensor singleton for normal operation
    bool init(ScanI2C::FoundDevice device);

    // Enable Wake on Motion interrupts (sensor must be initialised first)
    bool setWakeOnMotion();
};

class QMA6100PSensor : public MotionSensor
{
  private:
    QMA6100PSingleton *sensor = nullptr;

  public:
    explicit QMA6100PSensor(ScanI2C::FoundDevice foundDevice);

    // Initialise the motion sensor
    virtual bool init() override;

    // Called each time our sensor gets a chance to run
    virtual int32_t runOnce() override;
};

#endif

#endif