#pragma once

#include <math.h>

#include "mesh/generated/meshtastic/telemetry.pb.h"

namespace TelemetryPrecision
{
constexpr uint8_t kDefaultDecimals = 2;
constexpr uint8_t kVoltageDecimals = 3;
constexpr uint8_t kPressureDecimals = 1;

inline float roundTo(float value, uint8_t decimals)
{
    if (!isfinite(value)) {
        return value;
    }

    float scale = 1.0f;
    for (uint8_t i = 0; i < decimals; ++i) {
        scale *= 10.0f;
    }

    return roundf(value * scale) / scale;
}

inline void applyDeviceMetrics(meshtastic_DeviceMetrics &metrics)
{
    if (metrics.has_voltage) {
        metrics.voltage = roundTo(metrics.voltage, kVoltageDecimals);
    }
    if (metrics.has_channel_utilization) {
        metrics.channel_utilization = roundTo(metrics.channel_utilization, kDefaultDecimals);
    }
    if (metrics.has_air_util_tx) {
        metrics.air_util_tx = roundTo(metrics.air_util_tx, kDefaultDecimals);
    }
}

inline void applyEnvironmentMetrics(meshtastic_EnvironmentMetrics &metrics)
{
    if (metrics.has_temperature) {
        metrics.temperature = roundTo(metrics.temperature, kDefaultDecimals);
    }
    if (metrics.has_relative_humidity) {
        metrics.relative_humidity = roundTo(metrics.relative_humidity, kDefaultDecimals);
    }
    if (metrics.has_barometric_pressure) {
        metrics.barometric_pressure = roundTo(metrics.barometric_pressure, kPressureDecimals);
    }
    if (metrics.has_gas_resistance) {
        metrics.gas_resistance = roundTo(metrics.gas_resistance, kDefaultDecimals);
    }
    if (metrics.has_voltage) {
        metrics.voltage = roundTo(metrics.voltage, kVoltageDecimals);
    }
    if (metrics.has_current) {
        metrics.current = roundTo(metrics.current, kDefaultDecimals);
    }
    if (metrics.has_distance) {
        metrics.distance = roundTo(metrics.distance, kDefaultDecimals);
    }
    if (metrics.has_lux) {
        metrics.lux = roundTo(metrics.lux, kDefaultDecimals);
    }
    if (metrics.has_white_lux) {
        metrics.white_lux = roundTo(metrics.white_lux, kDefaultDecimals);
    }
    if (metrics.has_ir_lux) {
        metrics.ir_lux = roundTo(metrics.ir_lux, kDefaultDecimals);
    }
    if (metrics.has_uv_lux) {
        metrics.uv_lux = roundTo(metrics.uv_lux, kDefaultDecimals);
    }
    if (metrics.has_wind_speed) {
        metrics.wind_speed = roundTo(metrics.wind_speed, kDefaultDecimals);
    }
    if (metrics.has_weight) {
        metrics.weight = roundTo(metrics.weight, kDefaultDecimals);
    }
    if (metrics.has_wind_gust) {
        metrics.wind_gust = roundTo(metrics.wind_gust, kDefaultDecimals);
    }
    if (metrics.has_wind_lull) {
        metrics.wind_lull = roundTo(metrics.wind_lull, kDefaultDecimals);
    }
    if (metrics.has_radiation) {
        metrics.radiation = roundTo(metrics.radiation, kDefaultDecimals);
    }
    if (metrics.has_rainfall_1h) {
        metrics.rainfall_1h = roundTo(metrics.rainfall_1h, kDefaultDecimals);
    }
    if (metrics.has_rainfall_24h) {
        metrics.rainfall_24h = roundTo(metrics.rainfall_24h, kDefaultDecimals);
    }
    if (metrics.has_soil_temperature) {
        metrics.soil_temperature = roundTo(metrics.soil_temperature, kDefaultDecimals);
    }
}

inline void applyPowerMetrics(meshtastic_PowerMetrics &metrics)
{
    if (metrics.has_ch1_voltage) {
        metrics.ch1_voltage = roundTo(metrics.ch1_voltage, kVoltageDecimals);
    }
    if (metrics.has_ch1_current) {
        metrics.ch1_current = roundTo(metrics.ch1_current, kDefaultDecimals);
    }
    if (metrics.has_ch2_voltage) {
        metrics.ch2_voltage = roundTo(metrics.ch2_voltage, kVoltageDecimals);
    }
    if (metrics.has_ch2_current) {
        metrics.ch2_current = roundTo(metrics.ch2_current, kDefaultDecimals);
    }
    if (metrics.has_ch3_voltage) {
        metrics.ch3_voltage = roundTo(metrics.ch3_voltage, kVoltageDecimals);
    }
    if (metrics.has_ch3_current) {
        metrics.ch3_current = roundTo(metrics.ch3_current, kDefaultDecimals);
    }
    if (metrics.has_ch4_voltage) {
        metrics.ch4_voltage = roundTo(metrics.ch4_voltage, kVoltageDecimals);
    }
    if (metrics.has_ch4_current) {
        metrics.ch4_current = roundTo(metrics.ch4_current, kDefaultDecimals);
    }
    if (metrics.has_ch5_voltage) {
        metrics.ch5_voltage = roundTo(metrics.ch5_voltage, kVoltageDecimals);
    }
    if (metrics.has_ch5_current) {
        metrics.ch5_current = roundTo(metrics.ch5_current, kDefaultDecimals);
    }
    if (metrics.has_ch6_voltage) {
        metrics.ch6_voltage = roundTo(metrics.ch6_voltage, kVoltageDecimals);
    }
    if (metrics.has_ch6_current) {
        metrics.ch6_current = roundTo(metrics.ch6_current, kDefaultDecimals);
    }
    if (metrics.has_ch7_voltage) {
        metrics.ch7_voltage = roundTo(metrics.ch7_voltage, kVoltageDecimals);
    }
    if (metrics.has_ch7_current) {
        metrics.ch7_current = roundTo(metrics.ch7_current, kDefaultDecimals);
    }
    if (metrics.has_ch8_voltage) {
        metrics.ch8_voltage = roundTo(metrics.ch8_voltage, kVoltageDecimals);
    }
    if (metrics.has_ch8_current) {
        metrics.ch8_current = roundTo(metrics.ch8_current, kDefaultDecimals);
    }
}

inline void applyAirQualityMetrics(meshtastic_AirQualityMetrics &metrics)
{
    if (metrics.has_co2_temperature) {
        metrics.co2_temperature = roundTo(metrics.co2_temperature, kDefaultDecimals);
    }
    if (metrics.has_co2_humidity) {
        metrics.co2_humidity = roundTo(metrics.co2_humidity, kDefaultDecimals);
    }
    if (metrics.has_form_formaldehyde) {
        metrics.form_formaldehyde = roundTo(metrics.form_formaldehyde, kDefaultDecimals);
    }
    if (metrics.has_form_humidity) {
        metrics.form_humidity = roundTo(metrics.form_humidity, kDefaultDecimals);
    }
    if (metrics.has_form_temperature) {
        metrics.form_temperature = roundTo(metrics.form_temperature, kDefaultDecimals);
    }
    if (metrics.has_pm_temperature) {
        metrics.pm_temperature = roundTo(metrics.pm_temperature, kDefaultDecimals);
    }
    if (metrics.has_pm_humidity) {
        metrics.pm_humidity = roundTo(metrics.pm_humidity, kDefaultDecimals);
    }
    if (metrics.has_pm_voc_idx) {
        metrics.pm_voc_idx = roundTo(metrics.pm_voc_idx, kDefaultDecimals);
    }
    if (metrics.has_pm_nox_idx) {
        metrics.pm_nox_idx = roundTo(metrics.pm_nox_idx, kDefaultDecimals);
    }
    if (metrics.has_particles_tps) {
        metrics.particles_tps = roundTo(metrics.particles_tps, kDefaultDecimals);
    }
}

inline void applyLocalStats(meshtastic_LocalStats &stats)
{
    stats.channel_utilization = roundTo(stats.channel_utilization, kDefaultDecimals);
    stats.air_util_tx = roundTo(stats.air_util_tx, kDefaultDecimals);
}

inline void applyTelemetry(meshtastic_Telemetry &telemetry)
{
    switch (telemetry.which_variant) {
    case meshtastic_Telemetry_device_metrics_tag:
        applyDeviceMetrics(telemetry.variant.device_metrics);
        break;
    case meshtastic_Telemetry_environment_metrics_tag:
        applyEnvironmentMetrics(telemetry.variant.environment_metrics);
        break;
    case meshtastic_Telemetry_air_quality_metrics_tag:
        applyAirQualityMetrics(telemetry.variant.air_quality_metrics);
        break;
    case meshtastic_Telemetry_power_metrics_tag:
        applyPowerMetrics(telemetry.variant.power_metrics);
        break;
    case meshtastic_Telemetry_local_stats_tag:
        applyLocalStats(telemetry.variant.local_stats);
        break;
    default:
        break;
    }
}
} // namespace TelemetryPrecision
