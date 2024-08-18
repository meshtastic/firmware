#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR || !MESHTASTIC_EXCLUDE_POWER_TELEMETRY || !MESHTASTIC_EXCLUDE_POWERMON

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "MAX17048Sensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_MAX1704X.h>

MAX17048Sensor max17048Sensor;

MAX17048Sensor::MAX17048Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_MAX17048, "MAX17048") {}

int32_t MAX17048Sensor::runOnce()
{
    if (max17048Sensor.isInitialized())
    {
        LOG_INFO("Init sensor: %s is already initialised\n", sensorName);
        return true;
    }

    LOG_INFO("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }

    status = max17048.begin(nodeTelemetrySensorsMap[sensorType].second);
    return initI2CSensor();
}

void MAX17048Sensor::setup() {}

bool MAX17048Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    LOG_DEBUG("MAX17048Sensor::getMetrics id: %i\n",measurement->which_variant);

    float volts = max17048.cellVoltage();
    if (isnan(volts))
    {
        LOG_DEBUG("MAX17048Sensor::getMetrics battery is disconnected\n");
        return true;
    }

    float rate = max17048.chargeRate(); // charge/discharge rate in percent/hr
    float soc = max17048.cellPercent(); // state of charge in percent 0 to 100
    soc = soc > 100.0f ? 100.0f : (soc < 0.0f ? 0.0f : soc);
    // TODO fix this when MAX17048 is added to meshtastic 
    // protobufs https://github.com/meshtastic/protobufs/pull/541
    // float ttg = ((100.0f - soc) / rate) * 3600.0f;

    LOG_DEBUG("MAX17048Sensor::getMetrics volts: %.3f soc: %.3f\n", volts, soc);
    if((int) measurement->which_variant == meshtastic_Telemetry_power_metrics_tag)
    {
        measurement->variant.power_metrics.ch1_voltage = volts;
        measurement->variant.power_metrics.ch1_current = 0;
        measurement->variant.power_metrics.ch2_voltage = 0;
        measurement->variant.power_metrics.ch2_current = 0;
        measurement->variant.power_metrics.ch3_voltage = 0;
        measurement->variant.power_metrics.ch3_current = 0;
    }
    else if((int) measurement->which_variant == meshtastic_Telemetry_device_metrics_tag)
    {
        measurement->variant.device_metrics.battery_level = (uint32_t) round(soc);
        measurement->variant.device_metrics.voltage = volts;
        // TODO fix this when MAX17048 is added to meshtastic 
        // protobufs https://github.com/meshtastic/protobufs/pull/541
        //measurement->variant.device_metrics.battery_time_to_go_seconds = ttg;
    }
    return true;
}

uint16_t  MAX17048Sensor::getBusVoltageMv()
{
    float volts = max17048.cellVoltage();
    if (isnan(volts))
    {
        LOG_DEBUG("MAX17048Sensor::getMetrics battery is disconnected\n");
        return 0;
    }
    
    LOG_DEBUG("MAX17048Sensor::getBusVoltageMv %.3f\n", volts);
    return (uint16_t) (volts * 1000.0f);
}

uint8_t MAX17048Sensor::getBusBatteryPercent()
{
    float soc = max17048.cellPercent(); // state of charge in percent 0 to 100
    soc = soc > 100.0f ? 100.0f : (soc < 0.0f ? 0.0f : round(soc));
    LOG_DEBUG("MAX17048Sensor::getBusBatteryPercent %.1f\n", soc);
    return (uint8_t) soc;
}

uint16_t MAX17048Sensor::getTimeToGoSecs()
{
    float rate = max17048.chargeRate(); // charge/discharge rate in percent/hr
    float soc = max17048.cellPercent(); // state of charge in percent 0 to 100
    soc = soc > 100.0f ? 100.0f : (soc < 0.0f ? 0.0f : soc);
    float ttg = ((100.0f - soc) / rate)*3600.0f;
    LOG_DEBUG("MAX17048Sensor::getTimeToGoSecs %.0f\n", ttg);
    return (uint16_t) ttg;
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
    sample.chargeRate = max17048.chargeRate(); // charge/discharge rate in percent/hr
    sample.cellPercent = max17048.cellPercent(); // state of charge in percent 0 to 100
    samples.push(sample);

    while (samples.size()>MAX17048_SAMPLES)
        samples.pop();

    if (samples.front().chargeRate > MAX17048_MIN_CHARGE_RATE 
        && samples.back().chargeRate > MAX17048_MIN_CHARGE_RATE)
    {
        if (samples.front().cellPercent > samples.back().cellPercent)
            state = MAX17048ChargeState::EXPORT;
        else if (samples.front().cellPercent < samples.back().cellPercent)
            state = MAX17048ChargeState::IMPORT;
        else
            state = MAX17048ChargeState::IDLE;
    }

    LOG_DEBUG("MAX17048Sensor::isCharging %s\n", _chargeLabel[state] ); 
    return state == MAX17048ChargeState::IMPORT;
}

bool MAX17048Sensor::isBatteryConnected()
{
    float volts = max17048.cellVoltage();
    if (isnan(volts))
    {
        LOG_DEBUG("MAX17048Sensor::isBatteryConnected battery is disconnected\n");
        return false;
    }
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
    return volts >= MAX17048_MIN_USB_VOLTS;
} 

#endif