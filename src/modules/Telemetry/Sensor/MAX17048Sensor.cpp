#include "MAX17048Sensor.h"

#if !MESHTASTIC_EXCLUDE_I2C && !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL)

MAX17048Singleton *MAX17048Singleton::GetInstance()
{
    if (pinstance == nullptr) {
        pinstance = new MAX17048Singleton();
    }
    return pinstance;
}

MAX17048Singleton::MAX17048Singleton() {}

MAX17048Singleton::~MAX17048Singleton() {}

MAX17048Singleton *MAX17048Singleton::pinstance{nullptr};

bool MAX17048Singleton::runOnce(TwoWire *theWire)
{
    initialized = begin(theWire);
    LOG_DEBUG("%s::runOnce %s", sensorStr, initialized ? "began ok" : "begin failed");
    return initialized;
}

bool MAX17048Singleton::isBatteryCharging()
{
    float volts = cellVoltage();
    if (isnan(volts)) {
        LOG_DEBUG("%s::isBatteryCharging not connected", sensorStr);
        return 0;
    }

    MAX17048ChargeSample sample;
    sample.chargeRate = chargeRate();   // charge/discharge rate in percent/hr
    sample.cellPercent = cellPercent(); // state of charge in percent 0 to 100
    chargeSamples.push(sample);         // save a sample into a fifo buffer

    // Keep the fifo buffer trimmed
    while (chargeSamples.size() > MAX17048_CHARGING_SAMPLES)
        chargeSamples.pop();

    // Based on the past n samples, is the lipo charging, discharging or idle
    if (chargeSamples.front().chargeRate > MAX17048_CHARGING_MINIMUM_RATE &&
        chargeSamples.back().chargeRate > MAX17048_CHARGING_MINIMUM_RATE) {
        if (chargeSamples.front().cellPercent > chargeSamples.back().cellPercent)
            chargeState = MAX17048ChargeState::EXPORT;
        else if (chargeSamples.front().cellPercent < chargeSamples.back().cellPercent)
            chargeState = MAX17048ChargeState::IMPORT;
        else
            chargeState = MAX17048ChargeState::IDLE;
    } else {
        chargeState = MAX17048ChargeState::IDLE;
    }

    LOG_DEBUG("%s::isBatteryCharging %s volts: %.3f soc: %.3f rate: %.3f", sensorStr, chargeLabels[chargeState], volts,
              sample.cellPercent, sample.chargeRate);
    return chargeState == MAX17048ChargeState::IMPORT;
}

uint16_t MAX17048Singleton::getBusVoltageMv()
{
    float volts = cellVoltage();
    if (isnan(volts)) {
        LOG_DEBUG("%s::getBusVoltageMv is not connected", sensorStr);
        return 0;
    }
    LOG_DEBUG("%s::getBusVoltageMv %.3fmV", sensorStr, volts);
    return (uint16_t)(volts * 1000.0f);
}

uint8_t MAX17048Singleton::getBusBatteryPercent()
{
    float soc = cellPercent();
    LOG_DEBUG("%s::getBusBatteryPercent %.1f%%", sensorStr, soc);
    return clamp(static_cast<uint8_t>(round(soc)), static_cast<uint8_t>(0), static_cast<uint8_t>(100));
}

uint16_t MAX17048Singleton::getTimeToGoSecs()
{
    float rate = chargeRate();                     // charge/discharge rate in percent/hr
    float soc = cellPercent();                     // state of charge in percent 0 to 100
    soc = clamp(soc, 0.0f, 100.0f);                // clamp soc between 0 and 100%
    float ttg = ((100.0f - soc) / rate) * 3600.0f; // calculate seconds to charge/discharge
    LOG_DEBUG("%s::getTimeToGoSecs %.0f seconds", sensorStr, ttg);
    return (uint16_t)ttg;
}

bool MAX17048Singleton::isBatteryConnected()
{
    float volts = cellVoltage();
    if (isnan(volts)) {
        LOG_DEBUG("%s::isBatteryConnected is not connected", sensorStr);
        return false;
    }

    // if a valid voltage is returned, then battery must be connected
    return true;
}

bool MAX17048Singleton::isExternallyPowered()
{
    float volts = cellVoltage();
    if (isnan(volts)) {
        // if the battery is not connected then there must be external power
        LOG_DEBUG("%s::isExternallyPowered battery is", sensorStr);
        return true;
    }
    // if the bus voltage is over MAX17048_BUS_POWER_VOLTS, then the external power
    // is assumed to be connected
    LOG_DEBUG("%s::isExternallyPowered %s connected", sensorStr, volts >= MAX17048_BUS_POWER_VOLTS ? "is" : "is not");
    return volts >= MAX17048_BUS_POWER_VOLTS;
}

#if (HAS_TELEMETRY && (!MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR || !MESHTASTIC_EXCLUDE_POWER_TELEMETRY))

MAX17048Sensor::MAX17048Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_MAX17048, "MAX17048") {}

int32_t MAX17048Sensor::runOnce()
{
    if (isInitialized()) {
        LOG_INFO("Init sensor: %s is already initialised", sensorName);
        return true;
    }

    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    // Get a singleton instance and initialise the max17048
    if (max17048 == nullptr) {
        max17048 = MAX17048Singleton::GetInstance();
    }
    status = max17048->runOnce(nodeTelemetrySensorsMap[sensorType].second);
    return initI2CSensor();
}

void MAX17048Sensor::setup() {}

bool MAX17048Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    LOG_DEBUG("MAX17048 getMetrics id: %i", measurement->which_variant);

    float volts = max17048->cellVoltage();
    if (isnan(volts)) {
        LOG_DEBUG("MAX17048 getMetrics battery is not connected");
        return false;
    }

    float rate = max17048->chargeRate(); // charge/discharge rate in percent/hr
    float soc = max17048->cellPercent(); // state of charge in percent 0 to 100
    soc = clamp(soc, 0.0f, 100.0f);      // clamp soc between 0 and 100%
    float ttg = (100.0f - soc) / rate;   // calculate hours to charge/discharge

    LOG_DEBUG("MAX17048 getMetrics volts: %.3fV soc: %.1f%% ttg: %.1f hours", volts, soc, ttg);
    if ((int)measurement->which_variant == meshtastic_Telemetry_power_metrics_tag) {
        measurement->variant.power_metrics.has_ch1_voltage = true;
        measurement->variant.power_metrics.ch1_voltage = volts;
    } else if ((int)measurement->which_variant == meshtastic_Telemetry_device_metrics_tag) {
        measurement->variant.device_metrics.has_battery_level = true;
        measurement->variant.device_metrics.has_voltage = true;
        measurement->variant.device_metrics.battery_level = static_cast<uint32_t>(round(soc));
        measurement->variant.device_metrics.voltage = volts;
    }
    return true;
}

uint16_t MAX17048Sensor::getBusVoltageMv()
{
    return max17048->getBusVoltageMv();
};

#endif

#endif