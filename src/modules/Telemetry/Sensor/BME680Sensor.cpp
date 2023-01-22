#include "BME680Sensor.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "configuration.h"
#include <Adafruit_BME680.h>

BME680Sensor::BME680Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_BME680, "BME680") {}

int32_t BME680Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    status = bme680.begin(nodeTelemetrySensorsMap[sensorType]);

    return initI2CSensor();
}

void BME680Sensor::setup() {}

bool BME680Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    bme680.performReading();
    measurement->variant.environment_metrics.temperature = bme680.temperature;
    measurement->variant.environment_metrics.relative_humidity = bme680.humidity;
    measurement->variant.environment_metrics.barometric_pressure = bme680.pressure / 100.0F;
    measurement->variant.environment_metrics.gas_resistance = bme680.gas_resistance / 1000.0;

    return true;
}