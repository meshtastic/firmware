#include "../mesh/generated/environmental_measurement.pb.h"
#include "configuration.h"
#include "EnvironmentalMeasurementSensor.h"
#include "BME280Sensor.h"
#include <Adafruit_BME280.h>

BME280Sensor::BME280Sensor() : EnvironmentalMeasurementSensor {} {
}

int32_t BME280Sensor::runOnce() {
    unsigned bme280Status;
    // Default i2c address for BME280
    bme280Status = bme280.begin(0x76); 
    if (!bme280Status) {
        DEBUG_MSG("Could not find a valid BME280 sensor, check wiring, address, sensor ID!");
        // TODO more verbose diagnostics
    } else {
        DEBUG_MSG("EnvironmentalMeasurement: Opened BME280 on default i2c bus");
    }
    return BME_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
}

bool BME280Sensor::getMeasurement(EnvironmentalMeasurement *measurement) {
    measurement->temperature = bme280.readTemperature();
    measurement->relative_humidity = bme280.readHumidity();
    measurement->barometric_pressure = bme280.readPressure() / 100.0F;

    return true;
}    