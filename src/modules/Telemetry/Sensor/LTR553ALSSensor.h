#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && (defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1)) && \
    defined(HAS_LTR553ALS) && __has_include(<SensorLTR553.hpp>)

#pragma once

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
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
