#pragma once

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(HAS_LTR553ALS) && __has_include(<SensorLTR553.hpp>)

#include "mesh/generated/meshtastic/telemetry.pb.h"
#include "modules/Telemetry/Sensor/TelemetrySensor.h"
#include <SensorLTR553.hpp>

class LTR553ALSSensor : public TelemetrySensor
{
  public:
    LTR553ALSSensor();
    bool getMetrics(meshtastic_Telemetry *measurement) override;
    bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;

  private:
    SensorLTR553 sensor;
};

#endif
