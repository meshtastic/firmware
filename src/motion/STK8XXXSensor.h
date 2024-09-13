#pragma once
#ifndef _STK8XXX_SENSOR_H_
#define _STK8XXX_SENSOR_H_

#include "MotionSensor.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR 

#ifdef STK8XXX_INT

#include <stk8baxx.h>

class STK8XXXSensor : public MotionSensor
{
  private:
    STK8xxx sensor;

  public:
      STK8XXXSensor(ScanI2C::DeviceAddress address);
      virtual bool init() override;
      virtual int32_t runOnce() override;
};
#else

// Stub
class STK8XXXSensor: public MotionSensor
{
  public:
    STK8XXXSensor(ScanI2C::DeviceAddress address);
};

#endif

#endif

#endif