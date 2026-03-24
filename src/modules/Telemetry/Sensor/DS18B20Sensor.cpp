#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<DallasTemperature.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "DS18B20Sensor.h"
#include "TelemetrySensor.h"
#include <algorithm>
#include <cstring>

DS18B20Sensor::DS18B20Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_DS18B20, "DS18B20") {}

void DS18B20Sensor::scanBus()
{
    int count = dallas->getDeviceCount();
    LOG_INFO("DS18B20 scanBus on pin %d: %d device(s) found on bus", configuredPin, count);

    if (count == 0) {
        sensorCache.clear();
        return;
    }

    std::vector<SensorReading> found;
    for (int i = 0; i < count && i < MAX_DS18B20_SENSORS; i++) {
        DeviceAddress addr;
        if (!dallas->getAddress(addr, i)) {
            LOG_WARN("DS18B20: failed to read address for device %d", i);
            continue;
        }

        LOG_INFO("DS18B20: device %d ROM %02X%02X%02X%02X%02X%02X%02X%02X (family 0x%02X)", i, addr[0], addr[1], addr[2], addr[3],
                 addr[4], addr[5], addr[6], addr[7], addr[0]);

        if (!dallas->validFamily(addr)) {
            LOG_WARN("DS18B20: device %d skipped — not a DS18B20 (family 0x%02X, expected 0x28)", i, addr[0]);
            continue;
        }

        SensorReading sr;
        memcpy(sr.addr, addr, 8);
        sr.temperature = DEVICE_DISCONNECTED_C;

        // Preserve cached temperature if this ROM was already known
        for (const auto &cached : sensorCache) {
            if (memcmp(cached.addr, addr, 8) == 0) {
                sr.temperature = cached.temperature;
                break;
            }
        }
        found.push_back(sr);
    }

    // Sort by ROM code for stable, deterministic ordering across reboots
    std::sort(found.begin(), found.end(),
              [](const SensorReading &a, const SensorReading &b) { return memcmp(a.addr, b.addr, 8) < 0; });

    int newCount = (int)found.size() - (int)sensorCache.size();
    if (newCount > 0)
        LOG_INFO("DS18B20: %d new sensor(s) discovered", newCount);

    sensorCache = found;
}

int32_t DS18B20Sensor::runOnce()
{
    uint8_t newPin = moduleConfig.telemetry.ds18b20.pin;
    if (initialized && newPin != configuredPin) {
        LOG_INFO("DS18B20: pin changed %d → %d, reinitializing", configuredPin, newPin);
        delete dallas;
        delete oneWire;
        dallas = nullptr;
        oneWire = nullptr;
        sensorCache.clear();
        conversionPending = false;
        initialized = false;
    }

    if (!initialized) {
        configuredPin = newPin;
        if (!configuredPin) {
            LOG_WARN("DS18B20: no pin set — configure moduleConfig.telemetry.ds18b20.pin");
            return 60000;
        }
        LOG_INFO("Init sensor: %s on pin %d", sensorName, configuredPin);
#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3)
        if (configuredPin > 33) {
            LOG_WARN("DS18B20: GPIO %d may not work reliably — the upstream OneWire library "
                     "does not fully support GPIO > 33 on ESP32/ESP32-S3. Use GPIO <= 33 "
                     "or wait for upstream OneWire to be patched.",
                     configuredPin);
        }
#endif
        oneWire = new OneWire(configuredPin);
        dallas = new DallasTemperature(oneWire);

        dallas->begin();
        dallas->setWaitForConversion(false);
        dallas->setResolution(12);
        scanBus();
        if (sensorCache.empty())
            LOG_WARN("DS18B20: no sensors found on pin %d (will retry)", configuredPin);
        else
            LOG_INFO("DS18B20: found %d sensor(s)", (int)sensorCache.size());
        status = 1;
        initialized = true;
    }

    if (!conversionPending) {
        // Phase A: read results from the previous conversion into cache, then start a new one.
        // On the very first call, getTempC() returns DEVICE_DISCONNECTED_C (safe to ignore).
        for (auto &sr : sensorCache) {
            float t = dallas->getTempC(sr.addr);
            if (t != DEVICE_DISCONNECTED_C)
                sr.temperature = t;
        }

        // Rescan the bus to discover any sensors added since last cycle
        scanBus();

        // Kick off next conversion for all sensors in one broadcast command
        dallas->requestTemperatures();
        conversionPending = true;

        return 800; // Return in 800ms (>750ms worst-case for 12-bit conversion)
    } else {
        // Phase B: 800ms elapsed, conversion is complete — latch the final readings
        for (auto &sr : sensorCache) {
            float t = dallas->getTempC(sr.addr);
            if (t != DEVICE_DISCONNECTED_C) {
                sr.temperature = t;
                LOG_DEBUG("DS18B20 ROM %02X%02X%02X%02X%02X%02X%02X%02X: %.2f C", sr.addr[0], sr.addr[1], sr.addr[2], sr.addr[3],
                          sr.addr[4], sr.addr[5], sr.addr[6], sr.addr[7], t);
            }
        }
        conversionPending = false;

        // If still no sensors, retry in 15s; otherwise sleep until next telemetry interval
        if (sensorCache.empty()) {
            LOG_WARN("DS18B20: still no sensors on pin %d, retrying in 15s", configuredPin);
            return 15000;
        }
        return INT32_MAX;
    }
}

// FNV-1a 32-bit hash — compacts the full 64-bit ROM into a stable 32-bit sensor ID
static uint32_t romToId(const uint8_t addr[8])
{
    uint32_t hash = 2166136261u; // FNV offset basis
    for (int i = 0; i < 8; i++) {
        hash ^= addr[i];
        hash *= 16777619u; // FNV prime
    }
    return hash;
}

bool DS18B20Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if (sensorCache.empty())
        return false;

    auto &env = measurement->variant.environment_metrics;
    env.ds18b20_readings_count = 0;

    for (const auto &sr : sensorCache) {
        if (sr.temperature == DEVICE_DISCONNECTED_C)
            continue;
        if (env.ds18b20_readings_count >= MAX_DS18B20_SENSORS)
            break;

        meshtastic_DS18B20Reading &reading = env.ds18b20_readings[env.ds18b20_readings_count++];
        reading.device_id = romToId(sr.addr);
        reading.temperature = sr.temperature;
    }

    // Populate the legacy scalar temperature field according to the configured mode
    if (env.ds18b20_readings_count > 0 && !env.has_temperature) {
        float agg = env.ds18b20_readings[0].temperature;
        auto mode = moduleConfig.telemetry.ds18b20.mode;
        if (mode == meshtastic_ModuleConfig_DS18B20Config_TemperatureMode_AVERAGE ||
            mode == meshtastic_ModuleConfig_DS18B20Config_TemperatureMode_MIN ||
            mode == meshtastic_ModuleConfig_DS18B20Config_TemperatureMode_MAX) {
            for (pb_size_t i = 1; i < env.ds18b20_readings_count; i++) {
                float t = env.ds18b20_readings[i].temperature;
                if (mode == meshtastic_ModuleConfig_DS18B20Config_TemperatureMode_AVERAGE)
                    agg += t;
                else if (mode == meshtastic_ModuleConfig_DS18B20Config_TemperatureMode_MIN && t < agg)
                    agg = t;
                else if (mode == meshtastic_ModuleConfig_DS18B20Config_TemperatureMode_MAX && t > agg)
                    agg = t;
            }
            if (mode == meshtastic_ModuleConfig_DS18B20Config_TemperatureMode_AVERAGE)
                agg /= env.ds18b20_readings_count;
        }
        env.has_temperature = true;
        env.temperature = agg;
    }

    return env.ds18b20_readings_count > 0;
}

bool DS18B20Sensor::getMetricsChunk(meshtastic_Telemetry *measurement, int offset)
{
    auto &env = measurement->variant.environment_metrics;
    env.ds18b20_readings_count = 0;

    int idx = 0;
    for (const auto &sr : sensorCache) {
        if (idx++ < offset)
            continue;
        if (sr.temperature == DEVICE_DISCONNECTED_C)
            continue;
        if (env.ds18b20_readings_count >= MAX_DS18B20_SENSORS)
            break;

        meshtastic_DS18B20Reading &reading = env.ds18b20_readings[env.ds18b20_readings_count++];
        reading.device_id = romToId(sr.addr);
        reading.temperature = sr.temperature;
    }

    return env.ds18b20_readings_count > 0;
}

#endif // __has_include(<DallasTemperature.h>)
