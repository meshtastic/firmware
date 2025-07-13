#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<ClosedCube_OPT3001.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "OPT3001Sensor.h"
#include "TelemetrySensor.h"
#include <ClosedCube_OPT3001.h>

OPT3001Sensor::OPT3001Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_OPT3001, "OPT3001") {}

int32_t OPT3001Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    auto errorCode = opt3001.begin(nodeTelemetrySensorsMap[sensorType].first);
    status = errorCode == NO_ERROR;

    return initI2CSensor();
}

void OPT3001Sensor::setup()
{
    OPT3001_Config newConfig;

    newConfig.RangeNumber = 0b1100;
    newConfig.ConvertionTime = 0b0;
    newConfig.Latch = 0b1;
    newConfig.ModeOfConversionOperation = 0b11;

    OPT3001_ErrorCode errorConfig = opt3001.writeConfig(newConfig);
    if (errorConfig != NO_ERROR) {
        LOG_ERROR("OPT3001 configuration error #%d", errorConfig);
    }
}

bool OPT3001Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_lux = true;
    OPT3001 result = opt3001.readResult();

    measurement->variant.environment_metrics.lux = result.lux;
    LOG_INFO("Lux: %f", measurement->variant.environment_metrics.lux);

    return true;
}

#endif