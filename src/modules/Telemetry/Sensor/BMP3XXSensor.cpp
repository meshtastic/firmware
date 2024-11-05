#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "BMP3XXSensor.h"

BMP3XXSensor::BMP3XXSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_BMP3XX, "BMP3XX") {}

void BMP3XXSensor::setup() {}

int32_t BMP3XXSensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    // Get a singleton instance and initialise the bmp3xx
    if (bmp3xx == nullptr) {
        bmp3xx = BMP3XXSingleton::GetInstance();
    }
    status = bmp3xx->begin_I2C(nodeTelemetrySensorsMap[sensorType].first, nodeTelemetrySensorsMap[sensorType].second);

    // set up oversampling and filter initialization
    bmp3xx->setTemperatureOversampling(BMP3_OVERSAMPLING_4X);
    bmp3xx->setPressureOversampling(BMP3_OVERSAMPLING_8X);
    bmp3xx->setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_3);
    bmp3xx->setOutputDataRate(BMP3_ODR_25_HZ);

    // take a couple of initial readings to settle the sensor filters
    for (int i = 0; i < 3; i++) {
        bmp3xx->performReading();
    }
    return initI2CSensor();
}

bool BMP3XXSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if (bmp3xx == nullptr) {
        bmp3xx = BMP3XXSingleton::GetInstance();
    }
    if ((int)measurement->which_variant == meshtastic_Telemetry_environment_metrics_tag) {
        bmp3xx->performReading();

        measurement->variant.environment_metrics.has_temperature = true;
        measurement->variant.environment_metrics.has_barometric_pressure = true;
        measurement->variant.environment_metrics.has_relative_humidity = false;

        measurement->variant.environment_metrics.temperature = static_cast<float>(bmp3xx->temperature);
        measurement->variant.environment_metrics.barometric_pressure = static_cast<float>(bmp3xx->pressure) / 100.0F;
        measurement->variant.environment_metrics.relative_humidity = 0.0f;

        LOG_DEBUG("BMP3XX getMetrics id: %i temp: %.1f press %.1f", measurement->which_variant,
                  measurement->variant.environment_metrics.temperature,
                  measurement->variant.environment_metrics.barometric_pressure);
    } else {
        LOG_DEBUG("BMP3XX getMetrics id: %i", measurement->which_variant);
    }
    return true;
}

// Get a singleton wrapper for an Adafruit_bmp3xx
BMP3XXSingleton *BMP3XXSingleton::GetInstance()
{
    if (pinstance == nullptr) {
        pinstance = new BMP3XXSingleton();
    }
    return pinstance;
}

BMP3XXSingleton::BMP3XXSingleton() {}

BMP3XXSingleton::~BMP3XXSingleton() {}

BMP3XXSingleton *BMP3XXSingleton::pinstance{nullptr};

bool BMP3XXSingleton::performReading()
{
    bool result = Adafruit_BMP3XX::performReading();
    if (result) {
        double atmospheric = this->pressure / 100.0;
        altitudeAmslMetres = 44330.0 * (1.0 - pow(atmospheric / SEAL_LEVEL_HPA, 0.1903));
    } else {
        altitudeAmslMetres = 0.0;
    }
    return result;
}

#endif