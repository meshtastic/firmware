#pragma once
#ifndef _MMC5983MA_SENSOR_H_
#define _MMC5983MA_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<SparkFun_MMC5983MA_Arduino_Library.h>)

#include <SparkFun_MMC5983MA_Arduino_Library.h>

class MMC5983MASensor : public MotionSensor
{
  private:
    SFE_MMC5983MA sensor;
    bool continuousMode = false;
    bool showingScreen = false;
    static constexpr const char *compassCalibrationFileName = "/prefs/compass_mmc5983ma.dat";
    float highestX = 0, lowestX = 0, highestY = 0, lowestY = 0, highestZ = 0, lowestZ = 0;

    bool readMagnetometer(float &xGauss, float &yGauss, float &zGauss);

  public:
    explicit MMC5983MASensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;
    virtual void calibrate(uint16_t forSeconds) override;
};

#endif

#endif
