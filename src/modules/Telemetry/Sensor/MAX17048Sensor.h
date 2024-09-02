#pragma once

#ifndef MAX17048_SENSOR_H
#define MAX17048_SENSOR_H

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR || !MESHTASTIC_EXCLUDE_POWER_TELEMETRY || !MESHTASTIC_EXCLUDE_POWERMON

// samples to store in a buffer to determine if the battery is charging or discharging
#define MAX17048_CHARGING_SAMPLES 3

// threshold to determine if the battery is on charge, in percent/hour
#define MAX17048_CHARGING_MINIMUM_RATE 1.0f

// threshold to determine if the board has bus power
#define MAX17048_BUS_POWER_VOLTS 4.195f

#include <queue>
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include <Adafruit_MAX1704X.h>
#include "meshUtils.h"
#include "TelemetrySensor.h"
#include "VoltageSensor.h"

struct MAX17048ChargeSample{         
  float cellPercent;      
  float chargeRate;  
};       

enum MAX17048ChargeState {
  IDLE,
  EXPORT,
  IMPORT
};

class MAX17048Sensor : public TelemetrySensor, VoltageSensor
{
  private:
    Adafruit_MAX17048 max17048;
    std::queue<MAX17048ChargeSample> chargeSamples;
    MAX17048ChargeState chargeState = IDLE;
    const String chargeLabels[3] = { F("idle"), F("export"), F("import") };

  protected:
    virtual void setup() override;

  public:
    MAX17048Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual uint16_t getBusVoltageMv() override;
    virtual uint8_t getBusBatteryPercent();
    virtual uint16_t getTimeToGoSecs();
    virtual bool isBatteryCharging();
    virtual bool isBatteryConnected();  
    virtual bool isExternallyPowered();
};

#endif

#endif