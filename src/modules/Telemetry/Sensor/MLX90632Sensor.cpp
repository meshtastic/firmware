#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<SparkFun_MLX90632_Arduino_Library.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "MLX90632Sensor.h"
#include "TelemetrySensor.h"

MLX90632Sensor::MLX90632Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_MLX90632, "MLX90632") {}

int32_t MLX90632Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    MLX90632::status returnError;
    if (mlx.begin(nodeTelemetrySensorsMap[sensorType].first, *nodeTelemetrySensorsMap[sensorType].second, returnError) ==
        true) // MLX90632 init
    {
        LOG_DEBUG("MLX90632 Init Succeed");
        status = true;
    } else {
        LOG_ERROR("MLX90632 Init Failed");
        status = false;
    }
    return initI2CSensor();
}

void MLX90632Sensor::setup() {}

bool MLX90632Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_temperature = true;
    measurement->variant.environment_metrics.temperature = mlx.getObjectTemp(); // Get the object temperature in Fahrenheit

    return true;
}

#endif