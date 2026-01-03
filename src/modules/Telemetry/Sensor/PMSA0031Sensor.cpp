#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "PMSA0031Sensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_PM25AQI.h>

PMSA0031Sensor::PMSA0031Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_PMSA003I, "PMSA0031") {}

int32_t PMSA0031Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    aqi = Adafruit_PM25AQI();
    delay(10000);
    aqi.begin_I2C();
    /*                   nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_PMSA003I].first = found.address.address;
                    nodeTelemetrySensorsMap[meshtastic_TelemetrySensorType_PMSA003I].second =
                        i2cScanner->fetchI2CBus(found.address););*/
    return initI2CSensor();
}

void PMSA0031Sensor::setup() {}

bool PMSA0031Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    uint16_t co2, error;
    float temperature, humidity;
    if (!aqi.read(&data)) {
        LOG_WARN("Skipping send measurements. Could not read AQIn");
        return false;
    }
    measurement->variant.air_quality_metrics.pm10_standard = data.pm10_standard;
    measurement->variant.air_quality_metrics.pm25_standard = data.pm25_standard;
    measurement->variant.air_quality_metrics.pm100_standard = data.pm100_standard;

    measurement->variant.air_quality_metrics.pm10_environmental = data.pm10_env;
    measurement->variant.air_quality_metrics.pm25_environmental = data.pm25_env;
    measurement->variant.air_quality_metrics.pm100_environmental = data.pm100_env;
    return true;
}

#endif