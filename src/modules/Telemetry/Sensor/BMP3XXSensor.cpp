#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "BMP3XXSensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_BMP3XX.h>
#include <typeinfo>

BMP3XXSensor::BMP3XXSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_BMP3XX, "BMP3XX"){}

BMP3XXSensor bmp3xxSensor;

void BMP3XXSensor::setup(){};

int32_t BMP3XXSensor::runOnce()
{
    LOG_INFO("Init sensor: %s\n", sensorName);
    if (!hasSensor())
    {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    status = bmp3xx.begin_I2C(nodeTelemetrySensorsMap[sensorType].first, nodeTelemetrySensorsMap[sensorType].second);

    // set up oversampling and filter initialization
    bmp3xx.setTemperatureOversampling(BMP3_OVERSAMPLING_4X);
    bmp3xx.setPressureOversampling(BMP3_OVERSAMPLING_8X);
    bmp3xx.setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_3);
    bmp3xx.setOutputDataRate(BMP3_ODR_25_HZ);

    // take a couple of initial readings to settle the sensor filters
    for (int i = 0; i < 3; i++)
    {
        bmp3xx.performReading();
    }
    return initI2CSensor();
}

bool BMP3XXSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if ((int)measurement->which_variant == meshtastic_Telemetry_environment_metrics_tag)
    {
        bmp3xx.performReading();
        measurement->variant.environment_metrics.temperature = bmp3xx.readTemperature();
        measurement->variant.environment_metrics.barometric_pressure = bmp3xx.readPressure() / 100.0F;
        LOG_DEBUG("BMP3XXSensor::getMetrics id: %i temp: %.1f press %.1f\n", measurement->which_variant, measurement->variant.environment_metrics.temperature, measurement->variant.environment_metrics.barometric_pressure);
    }
    else
    {
        LOG_DEBUG("BMP3XXSensor::getMetrics id: %i\n", measurement->which_variant);
    }
    return true;
}

float BMP3XXSensor::getAltitudeAMSL()
{
    return bmp3xx.readAltitude(SEAL_LEVEL_HPA);
}

#endif