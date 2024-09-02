#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR || !MESHTASTIC_EXCLUDE_POWER_TELEMETRY || !MESHTASTIC_EXCLUDE_POWERMON

#include "MAX17048Sensor.h"

MAX17048Sensor::MAX17048Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_MAX17048, "MAX17048") {}

int32_t MAX17048Sensor::runOnce()
{
    if (isInitialized())
    {
        LOG_INFO("Init sensor: %s is already initialised\n", sensorName);
        return true;
    }

    LOG_INFO("Init sensor: %s\n", sensorName);
    if (!hasSensor())
    {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    status = max17048.begin(nodeTelemetrySensorsMap[sensorType].second);
    return initI2CSensor();
}

void MAX17048Sensor::setup() {}

bool MAX17048Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    LOG_DEBUG("MAX17048Sensor::getMetrics id: %i\n", measurement->which_variant);

    float volts = max17048.cellVoltage();
    if (isnan(volts))
    {
        LOG_DEBUG("MAX17048Sensor::getMetrics battery is disconnected\n");
        return true;
    }

    float rate = max17048.chargeRate();     // charge/discharge rate in percent/hr
    float soc = max17048.cellPercent();     // state of charge in percent 0 to 100
    soc = clamp(round(soc),0.0f,100.0f);    // clamp soc between 0 and 100%
    float ttg = (100.0f - soc) / rate;      // calculate hours to charge/discharge

    LOG_DEBUG("MAX17048Sensor::getMetrics volts: %.3fV soc: %.1f%% ttg: %.1f hours\n", volts, soc, ttg);
    if ((int)measurement->which_variant == meshtastic_Telemetry_power_metrics_tag)
    {
        measurement->variant.power_metrics.has_ch1_voltage = true;
        measurement->variant.power_metrics.ch1_voltage = volts;
    }
    else if ((int)measurement->which_variant == meshtastic_Telemetry_device_metrics_tag)
    {
        measurement->variant.device_metrics.has_battery_level = true;
        measurement->variant.device_metrics.has_voltage = true;
        measurement->variant.device_metrics.battery_level = (uint32_t)round(soc);
        measurement->variant.device_metrics.voltage = volts;
    }
    return true;
}

uint16_t MAX17048Sensor::getBusVoltageMv()
{
    float volts = max17048.cellVoltage();
    if (isnan(volts))
    {
        LOG_DEBUG("MAX17048Sensor::getMetrics battery is disconnected\n");
        return 0;
    }

    LOG_DEBUG("MAX17048Sensor::getBusVoltageMv %.3fmV\n", volts);
    return (uint16_t)(volts * 1000.0f);
}

uint8_t MAX17048Sensor::getBusBatteryPercent()
{
    float soc = max17048.cellPercent();     // state of charge in percent 0 to 100
    soc = clamp(round(soc),0.0f,100.0f);    // clamp soc between 0 and 100%
    LOG_DEBUG("MAX17048Sensor::getBusBatteryPercent %.1f%%\n", soc);
    return static_cast<uint8_t>(soc);
}

uint16_t MAX17048Sensor::getTimeToGoSecs()
{
    float rate = max17048.chargeRate();             // charge/discharge rate in percent/hr
    float soc = max17048.cellPercent();             // state of charge in percent 0 to 100
    soc = clamp(round(soc),0.0f,100.0f);            // clamp soc between 0 and 100%
    float ttg = ((100.0f - soc) / rate) * 3600.0f;  // calculate seconds to charge/discharge
    LOG_DEBUG("MAX17048Sensor::getTimeToGoSecs %.0f seconds\n", ttg);
    return (uint16_t)ttg;
}

bool MAX17048Sensor::isBatteryCharging()
{
    float volts = max17048.cellVoltage();
    if (isnan(volts))
    {
        LOG_DEBUG("MAX17048Sensor::isCharging battery is disconnected\n");
        return 0;
    }

    MAX17048ChargeSample sample;
    sample.chargeRate = max17048.chargeRate();   // charge/discharge rate in percent/hr
    sample.cellPercent = max17048.cellPercent(); // state of charge in percent 0 to 100
    chargeSamples.push(sample);                  // save a sample into a fifo buffer

    // keep the fifo buffer trimmed
    while (chargeSamples.size() > MAX17048_CHARGING_SAMPLES)
        chargeSamples.pop();

    // based on the past n samples, is the lipo charging, discharging or idle
    if (chargeSamples.front().chargeRate > MAX17048_CHARGING_MINIMUM_RATE &&
        chargeSamples.back().chargeRate > MAX17048_CHARGING_MINIMUM_RATE)
    {
        if (chargeSamples.front().cellPercent > chargeSamples.back().cellPercent)
            chargeState = MAX17048ChargeState::EXPORT;
        else if (chargeSamples.front().cellPercent < chargeSamples.back().cellPercent)
            chargeState = MAX17048ChargeState::IMPORT;
        else
            chargeState = MAX17048ChargeState::IDLE;
    }
    else
    {
        chargeState = MAX17048ChargeState::IDLE;
    }

    LOG_DEBUG("MAX17048Sensor::isCharging %s volts: %.3f soc: %.3f rate: %.3f\n",
        chargeLabels[chargeState], volts, sample.cellPercent, sample.chargeRate);
    return chargeState == MAX17048ChargeState::IMPORT;
}

bool MAX17048Sensor::isBatteryConnected()
{
    float volts = max17048.cellVoltage();
    if (isnan(volts))
    {
        LOG_DEBUG("MAX17048Sensor::isBatteryConnected battery is disconnected\n");
        return false;
    }

    // if a valid voltage is returned, then battery must be connected
    return true;
}

bool MAX17048Sensor::isExternallyPowered()
{
    float volts = max17048.cellVoltage();
    if (isnan(volts))
    {
        LOG_DEBUG("MAX17048Sensor::isExternallyPowered battery is disconnected\n");
        return false;
    }
    // if the bus voltage is over MAX17048_BUS_POWER_VOLTS, then the battery is charging
    return volts >= MAX17048_BUS_POWER_VOLTS;
}

#endif