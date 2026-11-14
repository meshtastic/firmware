#pragma once
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#include "SENXXSensor.h"

// Thin identity wrapper around SENXXSensor for the SEN5X family (SEN50/54/55,
// I2C address SEN5X_ADDR / 0x69). All protocol/state-machine logic lives in
// SENXXSensor; the exact model is auto-detected in probe()/initDevice().
class SEN5XSensor : public SENXXSensor
{
  public:
    SEN5XSensor() : SENXXSensor(meshtastic_TelemetrySensorType_SEN5X, "SEN5X") { senXXStateFileName = "/prefs/sen5X.dat"; }
};

#endif
