#include "configuration.h"

//Based on the already given sensor by Mestatic 
#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_SoilMoistureSensor.h>)

#pragma once

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_SoilMoisture.h>

class SoilMoistureSensor : public TelemetrySensor
{
  private:
    Adafruit_SoilMoisture soil = Adafruit_SoilMoisture();

  protected:
    virtual void setup() override;

  public:
    SoilMoistureSensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif
