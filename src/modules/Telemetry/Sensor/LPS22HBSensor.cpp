#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_LPS2X.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "LPS22HBSensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_LPS2X.h>
#include <Adafruit_Sensor.h>

LPS22HBSensor::LPS22HBSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_LPS22, "LPS22HB") {}

int32_t LPS22HBSensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    status = lps22hb.begin_I2C(nodeTelemetrySensorsMap[sensorType].first, nodeTelemetrySensorsMap[sensorType].second);
    return initI2CSensor();
}

void LPS22HBSensor::setup()
{
    lps22hb.setDataRate(LPS22_RATE_10_HZ);
}

bool LPS22HBSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_temperature = true;
    measurement->variant.environment_metrics.has_barometric_pressure = true;

    sensors_event_t temp;
    sensors_event_t pressure;
    lps22hb.getEvent(&pressure, &temp);

    measurement->variant.environment_metrics.temperature = temp.temperature;
    measurement->variant.environment_metrics.barometric_pressure = pressure.pressure;

    return true;
}

#endif