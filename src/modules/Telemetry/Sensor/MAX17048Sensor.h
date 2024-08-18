#pragma once

#ifndef MAX17048_SENSOR_H
#define MAX17048_SENSOR_H

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR || !MESHTASTIC_EXCLUDE_POWER_TELEMETRY || !MESHTASTIC_EXCLUDE_POWERMON

#define MAX17048_SAMPLES 5
#define MAX17048_MIN_CHARGE_RATE 1.0f
#define MAX17048_MIN_USB_VOLTS 4.15f

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "VoltageSensor.h"
#include <Adafruit_MAX1704X.h>

#include<bits/stdc++.h>
 
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
    std::queue<MAX17048ChargeSample> samples;
    MAX17048ChargeState state = IDLE;

  protected:
    virtual void setup() override;

    const String _chargeLabel[3] = { F("idle"), F("export"), F("import") };

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