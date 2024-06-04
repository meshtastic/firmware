#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "DFRobotLarkSensor.h"
#include "TelemetrySensor.h"
#include "gps/GeoCoord.h"
#include <DFRobot_LarkWeatherStation.h>
#include <string>

DFRobotLarkSensor::DFRobotLarkSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_DFROBOT_LARK, "DFROBOT_LARK") {}

int32_t DFRobotLarkSensor::runOnce()
{
    LOG_INFO("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    lark = DFRobot_LarkWeatherStation_I2C(nodeTelemetrySensorsMap[sensorType].first, nodeTelemetrySensorsMap[sensorType].second);

    if (lark.begin() == 0) // DFRobotLarkSensor init
    {
        LOG_DEBUG("DFRobotLarkSensor Init Succeed\n");
        status = true;
    } else {
        LOG_ERROR("DFRobotLarkSensor Init Failed\n");
        status = false;
    }
    return initI2CSensor();
}

void DFRobotLarkSensor::setup() {}

bool DFRobotLarkSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.temperature = lark.getValue("Temp").toFloat();
    measurement->variant.environment_metrics.relative_humidity = lark.getValue("Humi").toFloat();
    measurement->variant.environment_metrics.wind_speed = lark.getValue("Speed").toFloat();
    measurement->variant.environment_metrics.wind_direction = GeoCoord::bearingToDegrees(lark.getValue("Dir").c_str());
    measurement->variant.environment_metrics.barometric_pressure = lark.getValue("Pressure").toFloat();

    LOG_INFO("Temperature: %f\n", measurement->variant.environment_metrics.temperature);
    LOG_INFO("Humidity: %f\n", measurement->variant.environment_metrics.relative_humidity);
    LOG_INFO("Wind Speed: %f\n", measurement->variant.environment_metrics.wind_speed);
    LOG_INFO("Wind Direction: %d\n", measurement->variant.environment_metrics.wind_direction);
    LOG_INFO("Barometric Pressure: %f\n", measurement->variant.environment_metrics.barometric_pressure);

    return true;
}

#endif