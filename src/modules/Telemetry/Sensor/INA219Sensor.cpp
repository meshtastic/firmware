#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "INA219Sensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_INA219.h>

#ifndef INA219_MULTIPLIER
#define INA219_MULTIPLIER 1.0f
#endif

INA219Sensor::INA219Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_INA219, "INA219") {}

int32_t INA219Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    if (!ina219.success()) {
        ina219 = Adafruit_INA219(nodeTelemetrySensorsMap[sensorType].first);
        status = ina219.begin(nodeTelemetrySensorsMap[sensorType].second);
    } else {
        status = ina219.success();
    }
    return initI2CSensor();
}

void INA219Sensor::setup() {}

bool INA219Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_voltage = true;
    measurement->variant.environment_metrics.has_current = true;

    measurement->variant.environment_metrics.voltage = ina219.getBusVoltage_V();
    measurement->variant.environment_metrics.current = ina219.getCurrent_mA() * INA219_MULTIPLIER;
    return true;
}

uint16_t INA219Sensor::getBusVoltageMv()
{
    return lround(ina219.getBusVoltage_V() * 1000);
}

#endif