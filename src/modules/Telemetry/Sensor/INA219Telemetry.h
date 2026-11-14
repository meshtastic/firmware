#pragma once

#include "mesh/generated/meshtastic/telemetry.pb.h"

template <typename Sensor> bool readIna219Metrics(Sensor &sensor, meshtastic_Telemetry &measurement, float currentMultiplier)
{
    if (measurement.which_variant != meshtastic_Telemetry_environment_metrics_tag &&
        measurement.which_variant != meshtastic_Telemetry_power_metrics_tag)
        return false;

    const float voltage = sensor.getBusVoltage_V();
    const float current = sensor.getCurrent_mA() * currentMultiplier;

    if (measurement.which_variant == meshtastic_Telemetry_environment_metrics_tag) {
        auto &metrics = measurement.variant.environment_metrics;
        metrics.has_voltage = true;
        metrics.has_current = true;
        metrics.voltage = voltage;
        metrics.current = current;
    } else {
        auto &metrics = measurement.variant.power_metrics;
        metrics.has_ch1_voltage = true;
        metrics.has_ch1_current = true;
        metrics.ch1_voltage = voltage;
        metrics.ch1_current = current;
    }

    return true;
}
