#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_MLX90614.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "MLX90614Sensor.h"
#include "TelemetrySensor.h"
MLX90614Sensor::MLX90614Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_MLX90614, "MLX90614") {}

int32_t MLX90614Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    if (mlx.begin(nodeTelemetrySensorsMap[sensorType].first, nodeTelemetrySensorsMap[sensorType].second) == true) // MLX90614 init
    {
        LOG_DEBUG("MLX90614 emissivity: %f", mlx.readEmissivity());
        if (fabs(MLX90614_EMISSIVITY - mlx.readEmissivity()) > 0.001) {
            mlx.writeEmissivity(MLX90614_EMISSIVITY);
            LOG_INFO("MLX90614 emissivity updated. In case of weird data, power cycle");
        }
        LOG_DEBUG("MLX90614 Init Succeed");
        status = true;
    } else {
        LOG_ERROR("MLX90614 Init Failed");
        status = false;
    }
    return initI2CSensor();
}

void MLX90614Sensor::setup() {}

bool MLX90614Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.temperature = mlx.readAmbientTempC();
    measurement->variant.environment_metrics.has_temperature = true;
    measurement->variant.health_metrics.temperature = mlx.readObjectTempC();
    measurement->variant.health_metrics.has_temperature = true;
    return true;
}

#endif
