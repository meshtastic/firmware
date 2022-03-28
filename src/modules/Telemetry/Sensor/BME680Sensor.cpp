#include "../mesh/generated/telemetry.pb.h"
#include "configuration.h"
#include "TelemetrySensor.h"
#include "BME680Sensor.h"
#include <Adafruit_BME680.h>

BME680Sensor::BME680Sensor() : TelemetrySensor {} {
}

int32_t BME680Sensor::runOnce() {
    unsigned bme680Status;
    // Default i2c address for BME680
    bme680Status = bme680.begin(0x76); 
    if (!bme680Status) {
        DEBUG_MSG("Could not find a valid BME680 sensor, check wiring, address, sensor ID!");
        // TODO more verbose TelemetrySensor
    } else {
        DEBUG_MSG("Telemetry: Opened BME680 on default i2c bus");
        // Set up oversampling and filter initialization
        bme680.setTemperatureOversampling(BME680_OS_8X);
        bme680.setHumidityOversampling(BME680_OS_2X);
        bme680.setPressureOversampling(BME680_OS_4X);
        bme680.setIIRFilterSize(BME680_FILTER_SIZE_3);
        bme680.setGasHeater(320, 150); // 320*C for 150 ms
    }
    return (BME_680_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS);
}

bool BME680Sensor::getMeasurement(Telemetry *measurement) {
    measurement->variant.environment_metrics.temperature = bme680.readTemperature();
    measurement->variant.environment_metrics.relative_humidity = bme680.readHumidity();
    measurement->variant.environment_metrics.barometric_pressure = bme680.readPressure() / 100.0F;
    measurement->variant.environment_metrics.gas_resistance = bme680.readGas() / 1000.0;

    return true;
}    