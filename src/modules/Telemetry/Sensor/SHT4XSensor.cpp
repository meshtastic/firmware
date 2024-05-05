#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SHT4XSensor.h"
#include "TelemetrySensor.h"
#include <SensirionI2cSht4x.h>

// macro definitions
// make sure that we use the proper definition of NO_ERROR
#ifdef NO_ERROR
#undef NO_ERROR
#endif
#define NO_ERROR 0

static char errorMessage[64];
static int16_t error;

SHT4XSensor::SHT4XSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SHT4X, "SHT4X") {}

int32_t SHT4XSensor::runOnce()
{
    LOG_INFO("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    uint32_t serialNumber = 0;

    sht4x.begin(*nodeTelemetrySensorsMap[sensorType].second, 0x44);

    error = sht4x.serialNumber(serialNumber);
    LOG_DEBUG("serialNumber : %x\n", serialNumber);
    if (error != NO_ERROR) {
        LOG_DEBUG("Error trying to execute serialNumber(): ");
        errorToString(error, errorMessage, sizeof errorMessage);
        LOG_DEBUG(errorMessage);
        status = 0;
    } else {
        status = 1;
    }

    return initI2CSensor();
}

void SHT4XSensor::setup()
{
    // Set up oversampling and filter initialization
}

bool SHT4XSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    float aTemperature = 0.0;
    float aHumidity = 0.0;
    sht4x.measureLowestPrecision(aTemperature, aHumidity);
    measurement->variant.environment_metrics.temperature = aTemperature;
    measurement->variant.environment_metrics.relative_humidity = aHumidity;

    return true;
}

#endif