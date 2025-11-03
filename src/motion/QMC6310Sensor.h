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

// Axis mapping and heading style controls
#ifndef QMC6310_SWAP_XY
#define QMC6310_SWAP_XY 0   // 0: normal, 1: swap X and Y
#endif
#ifndef QMC6310_X_SIGN
#define QMC6310_X_SIGN 1    // +1 or -1 to flip X
#endif
#ifndef QMC6310_Y_SIGN
#define QMC6310_Y_SIGN 1    // +1 or -1 to flip Y
#endif
#ifndef QMC6310_HEADING_STYLE
#define QMC6310_HEADING_STYLE 0 // 0: atan2(my, mx); 1: atan2(x, -y) (QST library style)
#endif

// Sensitivity (Gauss/LSB) based on range; we set RANGE_2G in init()
#ifndef QMC6310_SENS_GAUSS_PER_LSB
#define QMC6310_SENS_GAUSS_PER_LSB 0.0066f
#endif

#ifndef QMC6310_EXPECTED_FIELD_uT
#define QMC6310_EXPECTED_FIELD_uT 42.0f
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
    // Soft-iron scale (computed from half-ranges)
    float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f;

  public:
    explicit QMC6310Sensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;
};

#endif

#endif
