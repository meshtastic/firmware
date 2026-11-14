#pragma once
#ifndef _QMC6309_SENSOR_H_
#define _QMC6309_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<SensorQMC6309.hpp>)

// SensorQMC6309.hpp (SensorLib 0.4.1) references the isBitSet() macro in an inline method but never includes
// SensorLib.h where it is defined. Define it here (guarded) so the header compiles regardless of include order
// (pulling in SensorLib.h is unreliable - its #pragma once can already be tripped by an in-progress include).
#ifndef isBitSet
#define isBitSet(value, bit) (((value) & (1UL << (bit))) == (1UL << (bit)))
#endif
#include <SensorQMC6309.hpp>

class QMC6309Sensor : public MotionSensor
{
  private:
    SensorQMC6309 sensor;
    bool showingScreen = false;
    static constexpr const char *compassCalibrationFileName = "/prefs/compass_qmc6309.dat";
#ifdef ELECROW_ThinkNode_M9
    float highestX = -5.548, lowestX = -6.530, highestY = -6.638, lowestY = -7.637, highestZ = -6.676, lowestZ = -7.633;
#else
    float highestX = 0, lowestX = 0, highestY = 0, lowestY = 0, highestZ = 0, lowestZ = 0;
#endif

    bool readMagnetometer(float &xGauss, float &yGauss, float &zGauss);

  public:
    explicit QMC6309Sensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;
    virtual void calibrate(uint16_t forSeconds) override;
};

#endif

#endif
