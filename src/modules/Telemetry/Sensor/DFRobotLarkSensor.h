#pragma once

#ifndef _MT_DFROBOTLARKSENSOR_H
#define _MT_DFROBOTLARKSENSOR_H
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <DFRobot_LarkWeatherStation.h>
#include <string>

class DFRobotLarkSensor : public TelemetrySensor
{
  private:
    DFRobot_LarkWeatherStation_I2C lark = DFRobot_LarkWeatherStation_I2C();

  protected:
    virtual void setup() override;

  public:
    DFRobotLarkSensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif
#endif