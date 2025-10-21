#pragma once
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<ClosedCube_OPT3001.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <ClosedCube_OPT3001.h>

class OPT3001Sensor : public TelemetrySensor
{
  private:
    ClosedCube_OPT3001 opt3001;

  public:
    OPT3001Sensor();
#if WIRE_INTERFACES_COUNT > 1
    virtual bool onlyWire1() { return true; }
#endif
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
};

#endif