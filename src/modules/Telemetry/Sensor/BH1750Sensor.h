#pragma once
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<BH1750_WE.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <BH1750_WE.h>

class BH1750Sensor : public TelemetrySensor
{
  private:
    BH1750_WE bh1750;

  public:
    BH1750Sensor();
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
};

#endif
