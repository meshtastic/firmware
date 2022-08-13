#include "../mesh/generated/telemetry.pb.h"
#include "configuration.h"
#include "TelemetrySensor.h"
#include "BMP280Sensor.h"
#include <Adafruit_BMP280.h>
#include <typeinfo>

BMP280Sensor::BMP280Sensor() : 
    TelemetrySensor(TelemetrySensorType_BMP280, "BMP280")
{
}

int32_t BMP280Sensor::runOnce() {
    DEBUG_MSG("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    status = bmp280.begin(nodeTelemetrySensorsMap[sensorType]); 
    return initI2CSensor();
}

void BMP280Sensor::setup() { }

bool BMP280Sensor::getMetrics(Telemetry *measurement) {
    DEBUG_MSG("BMP280Sensor::getMetrics\n");
    measurement->variant.environment_metrics.temperature = bmp280.readTemperature();
    measurement->variant.environment_metrics.barometric_pressure = bmp280.readPressure() / 100.0F;

    return true;
}    
