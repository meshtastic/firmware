#pragma once

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<RAK12035_SoilMoisture.h>) && defined(RAK_4631)
#ifndef _MT_RAK12035VBSENSOR_H
#define _MT_RAK12035VBSENSOR_H
#endif

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "RAK12035_SoilMoisture.h"
#include "TelemetrySensor.h"
#include <Arduino.h>

class RAK12035Sensor : public TelemetrySensor
{
  private:
    RAK12035 sensor;
    void setup();

  public:
    RAK12035Sensor();
#if WIRE_INTERFACES_COUNT > 1
    virtual bool onlyWire1() { return true; }
#endif
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
};
#endif
