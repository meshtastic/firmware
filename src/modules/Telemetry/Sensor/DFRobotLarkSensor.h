#pragma once

#ifndef _MT_DFROBOTLARKSENSOR_H
#define _MT_DFROBOTLARKSENSOR_H
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<DFRobot_LarkWeatherStation.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <DFRobot_LarkWeatherStation.h>
#include <string>

class DFRobotLarkSensor : public TelemetrySensor
{
  private:
    DFRobot_LarkWeatherStation_I2C lark = DFRobot_LarkWeatherStation_I2C();

  public:
    DFRobotLarkSensor();
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
};

#endif
#endif