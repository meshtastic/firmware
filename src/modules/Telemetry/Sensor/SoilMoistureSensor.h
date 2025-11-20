/* #include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<I2CSoilMoistureSensor.h>)

#pragma once

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <I2CSoilMoistureSensor.h>

class SoilMoistureSensor : public TelemetrySensor
{
  private:
    I2CSoilMoistureSensor soilSensor;

  protected:
    virtual void setup() override;

  public:
    SoilMoistureSensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif */