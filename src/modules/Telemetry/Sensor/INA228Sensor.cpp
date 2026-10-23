#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include("INA228.h")

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "INA228.h"
#include "INA228Sensor.h"
#include "TelemetrySensor.h"

// Shunt resistor in Ohms and the largest current to measure, in Amperes. The default suits the common
// INA228 breakouts (Adafruit 5832 and the generic "R015" boards). Override per variant with -D.
#ifndef INA228_SHUNT_R
#define INA228_SHUNT_R 0.015f
#endif
#ifndef INA228_MAX_CURRENT
#define INA228_MAX_CURRENT 10.0f
#endif

// Full scale of the narrow shunt range (ADCRANGE=1): +-40.96 mV, four times the resolution of the
// +-163.84 mV default
static constexpr float INA228_NARROW_RANGE_V = 0.04096f;

INA228Sensor::INA228Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_INA228, "INA228") {}

int32_t INA228Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    if (!status) {
        // Only on first open: a fresh INA228 object has no calibration, and getCurrent() scales by it
        begin(nodeTelemetrySensorsMap[sensorType].second, nodeTelemetrySensorsMap[sensorType].first);
        status = ina228.begin();
        if (status)
            calibrate();
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

void INA228Sensor::calibrate()
{
    // Take the narrow range whenever the full-scale shunt voltage fits in it. The range goes first:
    // the SHUNT_CAL value setMaxCurrentShunt() writes depends on it.
    const bool narrow = INA228_MAX_CURRENT * INA228_SHUNT_R <= INA228_NARROW_RANGE_V;
    ina228.setADCRange(narrow);
    if (ina228.setMaxCurrentShunt(INA228_MAX_CURRENT, INA228_SHUNT_R) != 0)
        LOG_WARN("INA228: invalid shunt %.4f Ohm / max current %.2f A", INA228_SHUNT_R, INA228_MAX_CURRENT);
    else
        LOG_DEBUG("INA228: shunt %.4f Ohm, max %.2f A, %s range", INA228_SHUNT_R, INA228_MAX_CURRENT,
                  narrow ? "40.96 mV" : "163.84 mV");
}

bool INA228Sensor::getMetrics(meshtastic_Telemetry *measurement)
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

bool INA228Sensor::getEnvironmentMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_voltage = true;
    measurement->variant.environment_metrics.has_current = true;

    measurement->variant.environment_metrics.voltage = ina228.getBusVoltage();
    measurement->variant.environment_metrics.current = ina228.getMilliAmpere();

    return true;
}

bool INA228Sensor::getPowerMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.power_metrics.has_ch1_voltage = true;
    measurement->variant.power_metrics.has_ch1_current = true;

    measurement->variant.power_metrics.ch1_voltage = ina228.getBusVoltage();
    measurement->variant.power_metrics.ch1_current = ina228.getMilliAmpere();

    return true;
}

uint16_t INA228Sensor::getBusVoltageMv()
{
    return lround(ina228.getBusMilliVolt());
}

int16_t INA228Sensor::getCurrentMa()
{
    return lround(ina228.getMilliAmpere());
}

#endif
