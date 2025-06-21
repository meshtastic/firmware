#pragma once
#ifndef _STK8XXX_SENSOR_H_
#define _STK8XXX_SENSOR_H_

#include "MotionSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && defined(HAS_STK8XXX)

#ifdef STK8XXX_INT

#include <stk8baxx.h>

class STK8XXXSensor : public MotionSensor
{
  private:
    STK8xxx sensor;

  public:
    explicit STK8XXXSensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;
};

#else

// Stub
class STK8XXXSensor : public MotionSensor
{
  public:
    explicit STK8XXXSensor(ScanI2C::FoundDevice foundDevice);
};

#endif

#endif

#endif