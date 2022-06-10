#include "../mesh/generated/telemetry.pb.h"
#include "configuration.h"
#include "TelemetrySensor.h"
#include "BME280Sensor.h"
#include <Adafruit_BME280.h>

BME280Sensor::BME280Sensor() : TelemetrySensor {} 
{
}

int32_t BME280Sensor::runOnce() {
    unsigned bme280Status;
    DEBUG_MSG("Init sensor: TelemetrySensorType_BME280\n");
    if (!hasSensor(TelemetrySensorType_BME280)) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    bme280Status = bme280.begin(nodeTelemetrySensorsMap[TelemetrySensorType_BME280]); 
    if (!bme280Status) {
        DEBUG_MSG("Could not connect to any detected BME-280 sensor.\nRemoving from nodeTelemetrySensorsMap.\n");
        nodeTelemetrySensorsMap[TelemetrySensorType_BME280] = 0;
    } else {
        DEBUG_MSG("Opened BME280 on default i2c bus\n");
    }
    return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
}

bool BME280Sensor::getMeasurement(Telemetry *measurement) {
    DEBUG_MSG("BME280Sensor::getMeasurement\n");
    measurement->variant.environment_metrics.temperature = bme280.readTemperature();
    measurement->variant.environment_metrics.relative_humidity = bme280.readHumidity();
    measurement->variant.environment_metrics.barometric_pressure = bme280.readPressure() / 100.0F;

    return true;
}    