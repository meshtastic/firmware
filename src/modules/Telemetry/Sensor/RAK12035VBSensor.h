#pragma once

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && CAN_HOST_RAK12035VBSOIL
#ifndef _MT_RAK12035VBSENSOR_H
#define _MT_RAK12035VBSENSOR_H
#endif

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Arduino.h>
#include "RAK12035_SoilMoisture.h"

class RAK12035VBSensor : public TelemetrySensor
{
  private:
    RAK12035 sensor;

  protected:
    virtual void setup() override;

  public:
    RAK12035VBSensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif