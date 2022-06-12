#include "../mesh/generated/telemetry.pb.h"
#include "configuration.h"
#include "TelemetrySensor.h"
#include "BME280Sensor.h"
#include <Adafruit_BME280.h>
#include <typeinfo>

BME280Sensor::BME280Sensor() : 
    TelemetrySensor(TelemetrySensorType_BME280, "BME280")
{
}

int32_t BME280Sensor::runOnce() {
    DEBUG_MSG("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    status = bme280.begin(nodeTelemetrySensorsMap[sensorType]); 
    return initI2CSensor();
}

void BME280Sensor::setup() { }

bool BME280Sensor::getMetrics(Telemetry *measurement) {
    DEBUG_MSG("BME280Sensor::getMetrics\n");
    measurement->variant.environment_metrics.temperature = bme280.readTemperature();
    measurement->variant.environment_metrics.relative_humidity = bme280.readHumidity();
    measurement->variant.environment_metrics.barometric_pressure = bme280.readPressure() / 100.0F;

    return true;
}    