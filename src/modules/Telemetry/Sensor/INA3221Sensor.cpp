#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<INA3221.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "INA3221Sensor.h"
#include "TelemetrySensor.h"
#include <INA3221.h>

INA3221Sensor::INA3221Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_INA3221, "INA3221"){};

int32_t INA3221Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    if (!status) {
        // Re-initialise with the address and Wire bus from the telemetry sensors map.
        // (Rob Tillaart INA3221_RT takes address + TwoWire*, unlike sgtwilko which took Wire in begin().)
        ina3221 = INA3221(nodeTelemetrySensorsMap[sensorType].first, nodeTelemetrySensorsMap[sensorType].second);
        status = ina3221.begin();
        if (status) {
            // Default all three channels to a 0.1 Ω shunt resistor.
            // Override per-variant by defining INA3221_SHUNT_R_CH1/CH2/CH3 (in Ohms) in variant.h.
#ifndef INA3221_SHUNT_R_CH1
#define INA3221_SHUNT_R_CH1 0.1f
#endif
#ifndef INA3221_SHUNT_R_CH2
#define INA3221_SHUNT_R_CH2 0.1f
#endif
#ifndef INA3221_SHUNT_R_CH3
#define INA3221_SHUNT_R_CH3 0.1f
#endif
            ina3221.setShuntR(0, INA3221_SHUNT_R_CH1);
            ina3221.setShuntR(1, INA3221_SHUNT_R_CH2);
            ina3221.setShuntR(2, INA3221_SHUNT_R_CH3);
        }
    } else {
        // Already initialised; status stays true and initI2CSensor() returns next poll interval.
        status = true;
    }
    return initI2CSensor();
};

void INA3221Sensor::setup() {}

struct _INA3221Measurement INA3221Sensor::getMeasurement(uint8_t ch)
{
    struct _INA3221Measurement measurement;

    measurement.voltage = ina3221.getBusVoltage(ch); // Volts
    // getCurrent_mA() is used instead of getCurrent() because Rob Tillaart's getCurrent()
    // returns Amperes; the telemetry proto and VoltageSensor/CurrentSensor interfaces expect mA.
    measurement.current = ina3221.getCurrent_mA(ch); // milliAmps

    return measurement;
}

struct _INA3221Measurements INA3221Sensor::getMeasurements()
{
    struct _INA3221Measurements measurements;

    // INA3221 has 3 channels starting from 0
    for (int i = 0; i < 3; i++) {
        measurements.measurements[i] = getMeasurement((uint8_t)i);
    }

    return measurements;
}

bool INA3221Sensor::getMetrics(meshtastic_Telemetry *measurement)
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

bool INA3221Sensor::getEnvironmentMetrics(meshtastic_Telemetry *measurement)
{
    struct _INA3221Measurement m = getMeasurement(ENV_CH);

    measurement->variant.environment_metrics.has_voltage = true;
    measurement->variant.environment_metrics.has_current = true;

    measurement->variant.environment_metrics.voltage = m.voltage;
    measurement->variant.environment_metrics.current = m.current;

    return true;
}

bool INA3221Sensor::getPowerMetrics(meshtastic_Telemetry *measurement)
{
    struct _INA3221Measurements m = getMeasurements();

    measurement->variant.power_metrics.has_ch1_voltage = true;
    measurement->variant.power_metrics.has_ch1_current = true;
    measurement->variant.power_metrics.has_ch2_voltage = true;
    measurement->variant.power_metrics.has_ch2_current = true;
    measurement->variant.power_metrics.has_ch3_voltage = true;
    measurement->variant.power_metrics.has_ch3_current = true;

    // INA3221 channel indices are zero-based (0=CH1, 1=CH2, 2=CH3).
    measurement->variant.power_metrics.ch1_voltage = m.measurements[0].voltage;
    measurement->variant.power_metrics.ch1_current = m.measurements[0].current;
    measurement->variant.power_metrics.ch2_voltage = m.measurements[1].voltage;
    measurement->variant.power_metrics.ch2_current = m.measurements[1].current;
    measurement->variant.power_metrics.ch3_voltage = m.measurements[2].voltage;
    measurement->variant.power_metrics.ch3_current = m.measurements[2].current;

    return true;
}

uint16_t INA3221Sensor::getBusVoltageMv()
{
    return lround(ina3221.getBusVoltage_mV(BAT_CH));
}

int16_t INA3221Sensor::getCurrentMa()
{
    return lround(ina3221.getCurrent_mA(BAT_CH));
}

// Bus voltage register (0x02 + ch*2): bits [15:3] unsigned, 1 LSB = 8 mV (datasheet p.6).
// Voltage raw units: 1 count = 8 mV, so V_mV = raw * 8.
int16_t INA3221Sensor::getRawBusVoltage(uint8_t ch)
{
    return (int16_t)(ina3221.getRegister(0x02 + ch * 2) >> 3);
}

// Shunt voltage register (0x01 + ch*2): bits [15:3] signed two's complement, 1 LSB = 40 µV (datasheet p.6).
// Current raw units are shunt-voltage counts: 1 count = 40 uV, signed.
// I_mA = (raw * 40 uV) / R_mOhm, because uV / mOhm = mA.
// Example for 100 mOhm shunt: I_mA = raw * 40 / 100 = raw * 0.4.
int16_t INA3221Sensor::getRawShuntCurrent(uint8_t ch)
{
    return (int16_t)(ina3221.getRegister(0x01 + ch * 2) >> 3);
}

#endif