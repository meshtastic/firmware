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
    status = bme680.begin(nodeTelemetrySensorsMap[sensorType], false);

    // Set up oversampling and filter initialization
    bme680.setTemperatureOversampling(BME68X_OS_1X);
    bme680.setHumidityOversampling(BME68X_OS_1X);
    bme680.setPressureOversampling(BME68X_OS_1X);
    bme680.setIIRFilterSize(BME68X_FILTER_OFF);
    bme680.setODR(BME68X_ODR_1000_MS);
    // BME68X_FORCED_MODE is done in Lib Init already
    bme680.setGasHeater(320, 150); // 320*C for 150 ms

    return initI2CSensor();
}

void BME680Sensor::setup() { }

bool BME680Sensor::getMetrics(Telemetry *measurement) {
    bme680.performReading();
    measurement->variant.environment_metrics.temperature = bme680.temperature;
    measurement->variant.environment_metrics.relative_humidity = bme680.humidity;
    measurement->variant.environment_metrics.barometric_pressure = bme680.pressure / 100.0F;
    measurement->variant.environment_metrics.gas_resistance = bme680.gas_resistance / 1000.0;

    return true;
}    