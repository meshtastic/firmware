#include "../mesh/generated/telemetry.pb.h"
#include "configuration.h"
#include "TelemetrySensor.h"
#include "BME280Sensor.h"
#include <Adafruit_BME280.h>

BME280Sensor::BME280Sensor() : TelemetrySensor {} {
}

int32_t BME280Sensor::runOnce() {
    unsigned bme280Status;
    // Default i2c address for BME280
    bme280Status = bme280.begin(0x76); 
    if (!bme280Status) {
        DEBUG_MSG("Could not find a valid BME280 sensor, check wiring, address, sensor ID!");
        // TODO more verbose diagnostics
    } else {
        DEBUG_MSG("Telemetry: Opened BME280 on default i2c bus");
    }
    return BME_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
}

bool BME280Sensor::getMeasurement(Telemetry *measurement) {
    measurement->variant.environment_metrics.temperature = bme280.readTemperature();
    measurement->variant.environment_metrics.relative_humidity = bme280.readHumidity();
    measurement->variant.environment_metrics.barometric_pressure = bme280.readPressure() / 100.0F;

    return true;
}    