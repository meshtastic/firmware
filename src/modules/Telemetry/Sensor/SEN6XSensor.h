#pragma once
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#include "SENXXSensor.h"

// Thin identity wrapper around SENXXSensor for the SEN6X family (SEN62, SEN63C,
// SEN65, SEN66, SEN68, SEN69C - I2C address SEN6X_ADDR / 0x6B). All
// protocol/state-machine logic lives in SENXXSensor; the exact model is
// auto-detected in probe()/initDevice().
class SEN6XSensor : public SENXXSensor
{
  public:
    SEN6XSensor() : SENXXSensor(meshtastic_TelemetrySensorType_SEN6X, "SEN6X") { senXXStateFileName = "/prefs/sen6X.dat"; }
};

#endif
