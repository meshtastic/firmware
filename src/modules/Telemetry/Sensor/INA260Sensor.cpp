#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "INA260Sensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_INA260.h>

INA260Sensor::INA260Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_INA260, "INA260") {}

int32_t INA260Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    if (!status) {
        status = ina260.begin(nodeTelemetrySensorsMap[sensorType].first, nodeTelemetrySensorsMap[sensorType].second);
    }
    return initI2CSensor();
}

void INA260Sensor::setup() {}

bool INA260Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_voltage = true;
    measurement->variant.environment_metrics.has_current = true;

    // mV conversion to V
    measurement->variant.environment_metrics.voltage = ina260.readBusVoltage() / 1000;
    measurement->variant.environment_metrics.current = ina260.readCurrent();
    return true;
}

uint16_t INA260Sensor::getBusVoltageMv()
{
    return lround(ina260.readBusVoltage());
}

#endif