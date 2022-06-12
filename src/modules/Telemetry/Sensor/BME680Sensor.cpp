#include "../mesh/generated/telemetry.pb.h"
#include "configuration.h"
#include "TelemetrySensor.h"
#include "BME680Sensor.h"
#include <Adafruit_BME680.h>

BME680Sensor::BME680Sensor() : 
    TelemetrySensor(TelemetrySensorType_BME680, "BME680") 
{
}

int32_t BME680Sensor::runOnce() {
    DEBUG_MSG("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    status = bme680.begin(nodeTelemetrySensorsMap[sensorType]); 
    return initI2CSensor();
}

void BME680Sensor::setup() 
{
    // Set up oversampling and filter initialization
    bme680.setTemperatureOversampling(BME680_OS_8X);
    bme680.setHumidityOversampling(BME680_OS_2X);
    bme680.setPressureOversampling(BME680_OS_4X);
    bme680.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme680.setGasHeater(320, 150); // 320*C for 150 ms
}

bool BME680Sensor::getMetrics(Telemetry *measurement) {
    measurement->variant.environment_metrics.temperature = bme680.readTemperature();
    measurement->variant.environment_metrics.relative_humidity = bme680.readHumidity();
    measurement->variant.environment_metrics.barometric_pressure = bme680.readPressure() / 100.0F;
    measurement->variant.environment_metrics.gas_resistance = bme680.readGas() / 1000.0;

    return true;
}    