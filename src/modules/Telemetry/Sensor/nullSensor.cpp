#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "nullSensor.h"
#include <typeinfo>

NullSensor::NullSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SENSOR_UNSET, "nullSensor") {}

int32_t NullSensor::runOnce()
{
    return 0;
}

void NullSensor::setup() {}

bool NullSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    return false;
}
#endif