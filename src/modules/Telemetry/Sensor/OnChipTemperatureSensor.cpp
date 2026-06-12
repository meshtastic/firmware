#include "configuration.h"

#ifndef MESHTASTIC_ENABLE_ONCHIP_TEMPERATURE_SENSOR
#define MESHTASTIC_ENABLE_ONCHIP_TEMPERATURE_SENSOR 1
#endif

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && MESHTASTIC_ENABLE_ONCHIP_TEMPERATURE_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "OnChipTemperatureSensor.h"
#include "RadioLibInterface.h"
#include <math.h>

#if defined(ARCH_NRF52)
#include "nrf_soc.h"
#endif

static bool isPlausibleChipTemperature(float temperature)
{
    return isfinite(temperature) && temperature > -45.0f && temperature < 130.0f;
}

OnChipTemperatureSensor::OnChipTemperatureSensor()
    : TelemetrySensor(meshtastic_TelemetrySensorType_SENSOR_UNSET, "On-chip temperature")
{
    initialized = true;
}

bool OnChipTemperatureSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    float temperature = 0;

    if (!readMcuTemperature(temperature) && !readRadioTemperature(temperature)) {
        return false;
    }

    if (!isPlausibleChipTemperature(temperature)) {
        LOG_WARN("Reject implausible on-chip temperature: %.2f", temperature);
        return false;
    }

    if (measurement->variant.environment_metrics.has_temperature) {
        return false;
    }

    measurement->variant.environment_metrics.has_temperature = true;
    measurement->variant.environment_metrics.temperature = temperature;
    LOG_DEBUG("On-chip temperature getMetrics: %.1f", temperature);
    return true;
}

bool OnChipTemperatureSensor::readMcuTemperature(float &temperature)
{
#if defined(ARCH_ESP32)
    float sum = 0.0f;
    float raw = NAN;
    uint8_t count = 0;

    for (uint8_t i = 0; i < 5; i++) {
        float sample = temperatureRead();
        raw = sample;
        if (isPlausibleChipTemperature(sample)) {
            sum += sample;
            count++;
        }
        yield();
    }

    if (count < 3) {
        return false;
    }

    temperature = sum / count;
    LOG_DEBUG("On-chip temperature source=mcu raw=%.3f accepted=%.2f samples=%u/5", raw, temperature, count);
    return true;
#elif defined(ARCH_NRF52)
    int32_t quarterDegrees = 0;
    if (sd_temp_get(&quarterDegrees) == NRF_SUCCESS) {
        temperature = quarterDegrees / 4.0f;
        LOG_DEBUG("On-chip temperature source=mcu raw=%.3f accepted=%.2f", temperature, temperature);
        return true;
    }
#elif defined(ARCH_RP2040)
    temperature = analogReadTemp();
    LOG_DEBUG("On-chip temperature source=mcu raw=%.3f accepted=%.2f", temperature, temperature);
    return true;
#endif

    return false;
}

bool OnChipTemperatureSensor::readRadioTemperature(float &temperature)
{
    if (RadioLibInterface::instance && RadioLibInterface::instance->getChipTemperature(temperature)) {
        LOG_DEBUG("On-chip temperature source=radio raw=%.3f accepted=%.2f", temperature, temperature);
        return true;
    }

    return false;
}

#endif
