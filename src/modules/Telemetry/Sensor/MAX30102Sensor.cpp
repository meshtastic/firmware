#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "MAX30102Sensor.h"
#include "TelemetrySensor.h"

MAX30102Sensor::MAX30102Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_MAX30102, "MAX30102") {}

int32_t MAX30102Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    if (max30102.begin(*nodeTelemetrySensorsMap[sensorType].second, _speed, nodeTelemetrySensorsMap[sensorType].first) ==
        true) // MAX30102 init
    {
        max30102.enableDIETEMPRDY(); // Enable the temperature ready interrupt
        LOG_DEBUG("MAX30102 Init Succeed\n");
        status = true;
    } else {
        LOG_ERROR("MAX30102 Init Failed\n");
        status = false;
    }
    return initI2CSensor();
}

void MAX30102Sensor::setup() {}

bool MAX30102Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    float temp = max30102.readTemperature();
    measurement->variant.environment_metrics.temperature = temp;
    measurement->variant.environment_metrics.has_temperature = true;
    measurement->variant.health_metrics.temperature = temp;
    measurement->variant.health_metrics.has_temperature = true;
    return true;
}

#endif