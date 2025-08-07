#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include("INA226.h")

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "INA226.h"
#include "INA226Sensor.h"
#include "TelemetrySensor.h"

INA226Sensor::INA226Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_INA226, "INA226") {}

int32_t INA226Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    begin(nodeTelemetrySensorsMap[sensorType].second, nodeTelemetrySensorsMap[sensorType].first);

    if (!status) {
        status = ina226.begin();
    }
    return initI2CSensor();
}

void INA226Sensor::setup() {}

void INA226Sensor::begin(TwoWire *wire, uint8_t addr)
{
    _wire = wire;
    _addr = addr;
    ina226 = INA226(_addr, _wire);
    _wire->begin();
    ina226.setMaxCurrentShunt(0.8, 0.100);
}

bool INA226Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    switch (measurement->which_variant) {
    case meshtastic_Telemetry_environment_metrics_tag:
        return getEnvironmentMetrics(measurement);

    case meshtastic_Telemetry_power_metrics_tag:
        return getPowerMetrics(measurement);
    }

    // unsupported metric
    return false;
}

bool INA226Sensor::getEnvironmentMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_voltage = true;
    measurement->variant.environment_metrics.has_current = true;

    measurement->variant.environment_metrics.voltage = ina226.getBusVoltage();
    measurement->variant.environment_metrics.current = ina226.getCurrent_mA();

    return true;
}

bool INA226Sensor::getPowerMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.power_metrics.has_ch1_voltage = true;
    measurement->variant.power_metrics.has_ch1_current = true;

    measurement->variant.power_metrics.ch1_voltage = ina226.getBusVoltage();
    measurement->variant.power_metrics.ch1_current = ina226.getCurrent_mA();

    return true;
}

uint16_t INA226Sensor::getBusVoltageMv()
{
    return lround(ina226.getBusVoltage() * 1000);
}

int16_t INA226Sensor::getCurrentMa()
{
    return lround(ina226.getCurrent_mA());
}

#endif