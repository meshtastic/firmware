#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "INA3221Sensor.h"
#include "TelemetrySensor.h"
#include <INA3221.h>

INA3221Sensor::INA3221Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_INA3221, "INA3221"){};

int32_t INA3221Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    if (!status) {
        ina3221.begin(nodeTelemetrySensorsMap[sensorType].second);
        ina3221.setShuntRes(100, 100, 100); // 0.1 Ohm shunt resistors
        status = true;
    } else {
        status = true;
    }
    return initI2CSensor();
};

void INA3221Sensor::setup() {}

struct _INA3221Measurement INA3221Sensor::getMeasurement(ina3221_ch_t ch)
{
    struct _INA3221Measurement measurement;

    measurement.voltage = ina3221.getVoltage(ch);
    measurement.current = ina3221.getCurrent(ch);

    return measurement;
}

struct _INA3221Measurements INA3221Sensor::getMeasurements()
{
    struct _INA3221Measurements measurements;

    // INA3221 has 3 channels starting from 0
    for (int i = 0; i < 3; i++) {
        measurements.measurements[i] = getMeasurement((ina3221_ch_t)i);
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

    measurement->variant.environment_metrics.voltage = m.voltage;
    measurement->variant.environment_metrics.current = m.current;

    return true;
}

bool INA3221Sensor::getPowerMetrics(meshtastic_Telemetry *measurement)
{
    struct _INA3221Measurements m = getMeasurements();

    measurement->variant.power_metrics.ch1_voltage = m.measurements[INA3221_CH1].voltage;
    measurement->variant.power_metrics.ch1_current = m.measurements[INA3221_CH1].current;
    measurement->variant.power_metrics.ch2_voltage = m.measurements[INA3221_CH2].voltage;
    measurement->variant.power_metrics.ch2_current = m.measurements[INA3221_CH2].current;
    measurement->variant.power_metrics.ch3_voltage = m.measurements[INA3221_CH3].voltage;
    measurement->variant.power_metrics.ch3_current = m.measurements[INA3221_CH3].current;

    return true;
}

uint16_t INA3221Sensor::getBusVoltageMv()
{
    return lround(ina3221.getVoltage(BAT_CH) * 1000);
}

#endif