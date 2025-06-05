#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<DFRobot_RainfallSensor.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "DFRobotGravitySensor.h"
#include "TelemetrySensor.h"
#include <DFRobot_RainfallSensor.h>
#include <string>

DFRobotGravitySensor::DFRobotGravitySensor() : TelemetrySensor(meshtastic_TelemetrySensorType_DFROBOT_RAIN, "DFROBOT_RAIN") {}

int32_t DFRobotGravitySensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    gravity = DFRobot_RainfallSensor_I2C(nodeTelemetrySensorsMap[sensorType].second);
    status = gravity.begin();

    return initI2CSensor();
}

void DFRobotGravitySensor::setup()
{
    LOG_DEBUG("%s VID: %x, PID: %x, Version: %s", sensorName, gravity.vid, gravity.pid, gravity.getFirmwareVersion().c_str());
}

bool DFRobotGravitySensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_rainfall_1h = true;
    measurement->variant.environment_metrics.has_rainfall_24h = true;

    measurement->variant.environment_metrics.rainfall_1h = gravity.getRainfall(1);
    measurement->variant.environment_metrics.rainfall_24h = gravity.getRainfall(24);

    LOG_INFO("Rain 1h: %f mm", measurement->variant.environment_metrics.rainfall_1h);
    LOG_INFO("Rain 24h: %f mm", measurement->variant.environment_metrics.rainfall_24h);
    return true;
}

#endif