#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR || !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "NodeDB.h"
#include "TelemetrySensor.h"
#include "main.h"

// Shared humidity value for cross-sensor compensation
// Default to 50% if no humidity sensor is available
float lastEnvironmentHumidity = 50.0f;
bool hasValidHumidity = false;

#endif