#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include("INA228.h")

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "INA228.h"
#include "INA228Sensor.h"
#include "TelemetrySensor.h"

// Shunt resistor in Ohms. The default suits the Adafruit 5832 and generic "R015" breakouts; override with -D
#ifndef INA228_SHUNT_R
#define INA228_SHUNT_R 0.015f
#endif

INA228Sensor::INA228Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_INA228, "INA228") {}

int32_t INA228Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    if (!status) {
        begin(nodeTelemetrySensorsMap[sensorType].second, nodeTelemetrySensorsMap[sensorType].first);
        status = ina228.begin();
    }
    return initI2CSensor();
}

void INA228Sensor::setup() {}

void INA228Sensor::begin(TwoWire *wire, uint8_t addr)
{
    _wire = wire;
    _addr = addr;
    ina228 = INA228(_addr, _wire);
    _wire->begin();
}

// Current is shunt voltage over the shunt: no SHUNT_CAL write, so nothing to calibrate or fail
float INA228Sensor::readCurrentMa()
{
    return ina228.getShuntVoltage() * (1000.0f / INA228_SHUNT_R);
}

bool INA228Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if (measurement->which_variant != meshtastic_Telemetry_power_metrics_tag)
        return false;

    // A failed register read returns 0; only getLastError(), cleared by each read, tells it apart
    auto &metrics = measurement->variant.power_metrics;
    metrics.ch1_voltage = ina228.getBusVoltage();
    metrics.has_ch1_voltage = ina228.getLastError() == 0;
    metrics.ch1_current = readCurrentMa();
    metrics.has_ch1_current = ina228.getLastError() == 0;

    if (!metrics.has_ch1_voltage || !metrics.has_ch1_current)
        LOG_WARN("INA228: read failed");
    return metrics.has_ch1_voltage || metrics.has_ch1_current;
}

uint16_t INA228Sensor::getBusVoltageMv()
{
    return lroundf(ina228.getBusMilliVolt());
}

int16_t INA228Sensor::getCurrentMa()
{
    return lroundf(readCurrentMa());
}

#endif
