#pragma once

#ifndef _MT_DFROBOTGRAVITYSENSOR_H
#define _MT_DFROBOTGRAVITYSENSOR_H
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<DFRobot_RainfallSensor.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <DFRobot_RainfallSensor.h>
#include <string>

class DFRobotGravitySensor : public TelemetrySensor
{
  private:
    DFRobot_RainfallSensor_I2C *gravity = nullptr;

  public:
    DFRobotGravitySensor();
    ~DFRobotGravitySensor();
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
};

#endif
#endif