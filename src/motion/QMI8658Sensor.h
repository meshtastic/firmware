#pragma once
#ifndef _QMI8658_SENSOR_H_
#define _QMI8658_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<SensorQMI8658.hpp>) && defined(IMU_CS)

#include <SPI.h>
#include <SensorQMI8658.hpp>

class QMI8658Sensor : public MotionSensor
{
  private:
    SensorQMI8658 qmi;

    // Simple motion threshold in Gs above steady 1g
    static constexpr float MOTION_THRESHOLD_G = 0.15f; // ~0.15 g

  public:
    explicit QMI8658Sensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;
};

#endif

#endif
