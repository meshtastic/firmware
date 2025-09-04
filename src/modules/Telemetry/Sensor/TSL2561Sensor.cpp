#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_TSL2561_U.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TSL2561Sensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_TSL2561_U.h>

TSL2561Sensor::TSL2561Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_TSL2561, "TSL2561") {}

int32_t TSL2561Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    status = tsl.begin(nodeTelemetrySensorsMap[sensorType].second);

    return initI2CSensor();
}

void TSL2561Sensor::setup()
{
    tsl.setGain(TSL2561_GAIN_1X);
    tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_101MS);
}

bool TSL2561Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_lux = true;
    sensors_event_t event;
    tsl.getEvent(&event);
    measurement->variant.environment_metrics.lux = event.light;
    LOG_INFO("Lux: %f", measurement->variant.environment_metrics.lux);

    return true;
}

#endif
