#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "CustomI2CSensor.h"
#include "I2CClient.h"
#include "I2CDefinitions.h"
#include "TelemetrySensor.h"

CustomI2CSensor::CustomI2CSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_CUSTOM_SENSOR, "CUSTOM") {}

int32_t CustomI2CSensor::runOnce()
{
    LOG_INFO("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    } else {
        status = 1;
    }

    return initI2CSensor();
}

void CustomI2CSensor::setup()
{
    // populates lastMetricsRecieved when data is received
    Wire.requestFrom(MT_I2C_ADDRESS, meshtastic_EnvironmentMetrics_size);
    Wire.onReceive(onReceiveMetrics);
}

bool CustomI2CSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    // Wire.requestFrom(MT_I2C_ADDRESS, meshtastic_EnvironmentMetrics_size);
    // auto length = Wire.available();
    // LOG_DEBUG("CustomI2CSensor::getMetrics=%d\n", length);
    // uint8_t buffer[meshtastic_EnvironmentMetrics_size];
    // Wire.readBytes(buffer, length);
    // meshtastic_EnvironmentMetrics metrics = meshtastic_EnvironmentMetrics_init_zero;
    // proto_decode(buffer, meshtastic_EnvironmentMetrics_size, meshtastic_EnvironmentMetrics_fields, &metrics);

    measurement->which_variant = meshtastic_Telemetry_environment_metrics_tag;
    if (lastMetricsRecieved.has_temperature) {
        measurement->variant.environment_metrics.has_temperature = true;
        measurement->variant.environment_metrics.temperature = lastMetricsRecieved.temperature;
    }
    if (lastMetricsRecieved.has_relative_humidity) {
        measurement->variant.environment_metrics.has_relative_humidity = true;
        measurement->variant.environment_metrics.relative_humidity = lastMetricsRecieved.relative_humidity;
    }
    if (lastMetricsRecieved.has_barometric_pressure) {
        measurement->variant.environment_metrics.has_barometric_pressure = true;
        measurement->variant.environment_metrics.barometric_pressure = lastMetricsRecieved.barometric_pressure;
    }
    if (lastMetricsRecieved.has_gas_resistance) {
        measurement->variant.environment_metrics.has_gas_resistance = true;
        measurement->variant.environment_metrics.gas_resistance = lastMetricsRecieved.gas_resistance;
    }
    if (lastMetricsRecieved.has_iaq) {
        measurement->variant.environment_metrics.has_iaq = true;
        measurement->variant.environment_metrics.iaq = lastMetricsRecieved.iaq;
    }
    if (lastMetricsRecieved.has_voltage) {
        measurement->variant.environment_metrics.has_voltage = true;
        measurement->variant.environment_metrics.voltage = lastMetricsRecieved.voltage;
    }
    if (lastMetricsRecieved.has_current) {
        measurement->variant.environment_metrics.has_current = true;
        measurement->variant.environment_metrics.current = lastMetricsRecieved.current;
    }
    if (lastMetricsRecieved.has_distance) {
        measurement->variant.environment_metrics.has_distance = true;
        measurement->variant.environment_metrics.distance = lastMetricsRecieved.distance;
    }
    if (lastMetricsRecieved.has_lux) {
        measurement->variant.environment_metrics.has_lux = true;
        measurement->variant.environment_metrics.lux = lastMetricsRecieved.lux;
    }
    if (lastMetricsRecieved.has_white_lux) {
        measurement->variant.environment_metrics.has_white_lux = true;
        measurement->variant.environment_metrics.white_lux = lastMetricsRecieved.white_lux;
    }
    if (lastMetricsRecieved.has_ir_lux) {
        measurement->variant.environment_metrics.has_ir_lux = true;
        measurement->variant.environment_metrics.ir_lux = lastMetricsRecieved.ir_lux;
    }
    if (lastMetricsRecieved.has_uv_lux) {
        measurement->variant.environment_metrics.has_uv_lux = true;
        measurement->variant.environment_metrics.uv_lux = lastMetricsRecieved.uv_lux;
    }
    if (lastMetricsRecieved.has_wind_direction) {
        measurement->variant.environment_metrics.has_wind_direction = true;
        measurement->variant.environment_metrics.wind_direction = lastMetricsRecieved.wind_direction;
    }
    if (lastMetricsRecieved.has_wind_speed) {
        measurement->variant.environment_metrics.has_wind_speed = true;
        measurement->variant.environment_metrics.wind_speed = lastMetricsRecieved.wind_speed;
    }
    if (lastMetricsRecieved.has_wind_lull) {
        measurement->variant.environment_metrics.has_wind_lull = true;
        measurement->variant.environment_metrics.wind_lull = lastMetricsRecieved.wind_lull;
    }

    return true;
}

#endif