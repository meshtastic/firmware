#include "SHT31Sensor.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "configuration.h"
#include <Adafruit_SHT31.h>

SHT31Sensor::SHT31Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SHT31, "SHT31") {}

int32_t SHT31Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    status = sht31.begin();
    return initI2CSensor();
}

void SHT31Sensor::setup()
{
    // Set up oversampling and filter initialization
}

bool SHT31Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.temperature = sht31.readTemperature();
    measurement->variant.environment_metrics.relative_humidity = sht31.readHumidity();

    return true;
}
