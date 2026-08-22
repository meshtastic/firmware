#pragma once
#ifndef _ICM42607P_SENSOR_H_
#define _ICM42607P_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<ICM42670P.h>)

#include <memory>

class ICM42670;

class ICM42607PSensor : public MotionSensor
{
  private:
    std::unique_ptr<ICM42670> sensor;
    TwoWire *wire = nullptr;

  public:
    explicit ICM42607PSensor(ScanI2C::FoundDevice foundDevice);
    ~ICM42607PSensor() override;
    virtual bool init() override;
    virtual int32_t runOnce() override;
};

#endif

#endif
