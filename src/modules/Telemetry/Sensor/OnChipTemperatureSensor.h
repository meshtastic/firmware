#include "configuration.h"

#ifndef MESHTASTIC_ENABLE_ONCHIP_TEMPERATURE_SENSOR
#define MESHTASTIC_ENABLE_ONCHIP_TEMPERATURE_SENSOR 1
#endif

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && MESHTASTIC_ENABLE_ONCHIP_TEMPERATURE_SENSOR

#pragma once

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"

class OnChipTemperatureSensor : public TelemetrySensor
{
  public:
    OnChipTemperatureSensor();
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;

  private:
    bool readMcuTemperature(float &temperature);
    bool readRadioTemperature(float &temperature);
};

#endif
