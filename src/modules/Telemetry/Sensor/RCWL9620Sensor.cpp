#include "RCWL9620Sensor.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "configuration.h"

RCWL9620Sensor::RCWL9620Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_RCWL9620, "RCWL9620") {}

int32_t RCWL9620Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    status = 1;
    rcwl9620.begin(nodeTelemetrySensorsMap[sensorType].second, nodeTelemetrySensorsMap[sensorType].first, -1, -1);
    return initI2CSensor();
}

void RCWL9620Sensor::setup() {}

bool RCWL9620Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    LOG_DEBUG("RCWL9620Sensor::getMetrics\n");
    measurement->variant.environment_metrics.distance = rcwl9620.getDistance();
    return true;
}