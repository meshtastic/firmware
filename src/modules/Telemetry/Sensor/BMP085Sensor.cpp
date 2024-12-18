#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "BMP085Sensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_BMP085.h>
#include <typeinfo>

BMP085Sensor::BMP085Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_BMP085, "BMP085") {}

int32_t BMP085Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    bmp085 = Adafruit_BMP085();
    status = bmp085.begin(nodeTelemetrySensorsMap[sensorType].first, nodeTelemetrySensorsMap[sensorType].second);

    return initI2CSensor();
}

void BMP085Sensor::setup() {}

bool BMP085Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_temperature = true;
    measurement->variant.environment_metrics.has_barometric_pressure = true;

    LOG_DEBUG("BMP085 getMetrics");
    measurement->variant.environment_metrics.temperature = bmp085.readTemperature();
    measurement->variant.environment_metrics.barometric_pressure = bmp085.readPressure() / 100.0F;

    return true;
}

#endif