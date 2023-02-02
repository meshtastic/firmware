#include "BMP280Sensor.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "configuration.h"
#include <Adafruit_BMP280.h>
#include <typeinfo>

BMP280Sensor::BMP280Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_BMP280, "BMP280") {}

int32_t BMP280Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    status = bmp280.begin(nodeTelemetrySensorsMap[sensorType]);

    bmp280.setSampling(Adafruit_BMP280::MODE_FORCED,
                       Adafruit_BMP280::SAMPLING_X1, // Temp. oversampling
                       Adafruit_BMP280::SAMPLING_X1, // Pressure oversampling
                       Adafruit_BMP280::FILTER_OFF, Adafruit_BMP280::STANDBY_MS_1000);

    return initI2CSensor();
}

void BMP280Sensor::setup() {}

bool BMP280Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    LOG_DEBUG("BMP280Sensor::getMetrics\n");
    bmp280.takeForcedMeasurement();
    measurement->variant.environment_metrics.temperature = bmp280.readTemperature();
    measurement->variant.environment_metrics.barometric_pressure = bmp280.readPressure() / 100.0F;

    return true;
}
