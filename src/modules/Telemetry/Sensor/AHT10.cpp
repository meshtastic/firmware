#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "AHT10.h"
#include "TelemetrySensor.h"

#include <Adafruit_AHTX0.h>
#include <typeinfo>

AHT10Sensor::AHT10Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_AHT10, "AHT10") {}

int32_t AHT10Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    aht10 = Adafruit_AHTX0();
    status = aht10.begin(nodeTelemetrySensorsMap[sensorType].second, 0, nodeTelemetrySensorsMap[sensorType].first);

    return initI2CSensor();
}

void AHT10Sensor::setup() {}

bool AHT10Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    LOG_DEBUG("AHT10Sensor::getMetrics\n");

    sensors_event_t humidity, temp;
    aht10.getEvent(&humidity, &temp);

    measurement->variant.environment_metrics.temperature = temp.temperature;
    measurement->variant.environment_metrics.relative_humidity = humidity.relative_humidity;

    return true;
}

#endif