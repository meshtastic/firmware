#pragma once
#ifndef _QMC6310_SENSOR_H_
#define _QMC6310_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<SensorQMC6310.hpp>)

#include <SensorQMC6310.hpp>
#include <math.h>

#ifndef QMC6310_DECLINATION_DEG
#define QMC6310_DECLINATION_DEG 0.0f
#endif

#ifndef QMC6310_YAW_MOUNT_OFFSET
#define QMC6310_YAW_MOUNT_OFFSET 0.0f
#endif

class QMC6310Sensor : public MotionSensor
{
  private:
    SensorQMC6310 sensor;
    uint32_t lastLogMs = 0;
    // Hard-iron calibration tracking
    float minX = 1e9f, maxX = -1e9f;
    float minY = 1e9f, maxY = -1e9f;
    float minZ = 1e9f, maxZ = -1e9f;
    float offsetX = 0.0f, offsetY = 0.0f, offsetZ = 0.0f;

  public:
    explicit QMC6310Sensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;
};

#endif

#endif
