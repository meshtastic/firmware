#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "MLX90614Sensor.h"
#include "TelemetrySensor.h"

MLX90614Sensor::MLX90614Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_MLX90614, "MLX90614") {}

int32_t MLX90614Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    if (mlx.begin(nodeTelemetrySensorsMap[sensorType].first, *nodeTelemetrySensorsMap[sensorType].second) ==
        true) // MLX90614 init
    {
        LOG_DEBUG("MLX90614 Init Succeed\n");
        status = true;
    } else {
        LOG_ERROR("MLX90614 Init Failed\n");
        status = false;
    }
    return initI2CSensor();
}

void MLX90614Sensor::setup() {}

bool MLX90614Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if (mlx.read()) {
        measurement->variant.environment_metrics.has_temperature = true;
        measurement->variant.environment_metrics.temperature = mlx.object(); // Get the object temperature
        return true;
    } else {
        return false;
    }
}

#endif