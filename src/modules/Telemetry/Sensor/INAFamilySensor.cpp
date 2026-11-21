#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "INAFamilySensor.h"
#include "TelemetrySensor.h"
#include <Wire.h>
#include <cmath>

INAFamilySensor::INAFamilySensor(meshtastic_TelemetrySensorType type, const char *name, const InaChip &chip,
                                 const float (&shuntR)[3], uint8_t envChannel, uint8_t batteryChannel)
    : TelemetrySensor(type, name), chip(chip), envChannel(envChannel), batteryChannel(batteryChannel)
{
    for (uint8_t i = 0; i < 3; i++)
        this->shuntR[i] = shuntR[i];
}

int32_t INAFamilySensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor())
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;

    if (status)
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;

    address = nodeTelemetrySensorsMap[sensorType].first;
    wire = nodeTelemetrySensorsMap[sensorType].second;
    if (wire) {
        wire->begin();
        status = reset();
        if (status)
            delay(chip.channels == 3 ? 10 : 2);
    }
    return initI2CSensor();
}

bool INAFamilySensor::reset()
{
    const uint8_t command[3] = {0x00, 0x80, 0x00}; // reg 0x00, RST
    wire->beginTransmission(address);
    wire->write(command, sizeof(command));
    return wire->endTransmission() == 0;
}

bool INAFamilySensor::readReg(uint8_t reg, uint8_t bytes, uint32_t &out)
{
    wire->beginTransmission(address);
    wire->write(reg);
    if (wire->endTransmission() != 0)
        return false;
    if (wire->requestFrom(address, bytes) != bytes)
        return false;

    uint32_t value = 0;
    for (uint8_t i = 0; i < bytes; i++) {
        int byte = wire->read();
        if (byte < 0)
            return false;
        value = (value << 8) | (uint8_t)byte;
    }
    out = value;
    return true;
}

bool INAFamilySensor::readChannel(uint8_t channel, InaReading &out)
{
    uint32_t busRaw = 0;
    uint32_t shuntRaw = 0;
    uint8_t reg = chip.busReg + channel * 2;
    if (!readReg(reg, chip.bytes, busRaw))
        return false;
    reg = chip.shuntReg + channel * 2;
    if (!readReg(reg, chip.bytes, shuntRaw))
        return false;

    out = inaConvert(chip, busRaw, shuntRaw, shuntR[channel]);
    return true;
}

void INAFamilySensor::clearEnvironmentMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_voltage = false;
    measurement->variant.environment_metrics.has_current = false;
    measurement->variant.environment_metrics.voltage = 0;
    measurement->variant.environment_metrics.current = 0;
}

bool INAFamilySensor::getEnvironmentMetrics(meshtastic_Telemetry *measurement)
{
    if (!chip.environment)
        return false;

    InaReading reading;
    uint8_t channel = chip.channels == 3 ? envChannel : 0;
    if (!readChannel(channel, reading)) {
        clearEnvironmentMetrics(measurement);
        return false;
    }

    auto &env = measurement->variant.environment_metrics;
    env.has_voltage = true;
    env.voltage = reading.busV;
    env.has_current = true;
    env.current = inaScaleCurrent(chip, reading.currentMa);
    return true;
}

bool INAFamilySensor::getPowerMetrics(meshtastic_Telemetry *measurement)
{
    auto &p = measurement->variant.power_metrics;
    uint8_t channels = chip.channels;
    float *voltages[] = {&p.ch1_voltage, &p.ch2_voltage, &p.ch3_voltage};
    float *currents[] = {&p.ch1_current, &p.ch2_current, &p.ch3_current};
    bool *hasVoltages[] = {&p.has_ch1_voltage, &p.has_ch2_voltage, &p.has_ch3_voltage};
    bool *hasCurrents[] = {&p.has_ch1_current, &p.has_ch2_current, &p.has_ch3_current};

    bool valid = false;
    for (uint8_t i = 0; i < channels; i++) {
        InaReading reading;
        bool got = readChannel(i, reading);
        *hasVoltages[i] = got;
        *hasCurrents[i] = got;
        *voltages[i] = got ? reading.busV : 0;
        *currents[i] = got ? inaScaleCurrent(chip, reading.currentMa) : 0;
        valid = valid || got;
    }
    for (uint8_t i = channels; i < 3; i++) {
        *hasVoltages[i] = false;
        *hasCurrents[i] = false;
        *voltages[i] = 0;
        *currents[i] = 0;
    }
    return valid;
}

bool INAFamilySensor::getMetrics(meshtastic_Telemetry *measurement)
{
    switch (measurement->which_variant) {
    case meshtastic_Telemetry_environment_metrics_tag:
        return getEnvironmentMetrics(measurement);
    case meshtastic_Telemetry_power_metrics_tag:
        return getPowerMetrics(measurement);
    }
    return false;
}

uint16_t INAFamilySensor::getBusVoltageMv()
{
    InaReading reading;
    if (!readChannel(batteryChannel, reading))
        return 0;
    return lround(reading.busV * 1000.0f);
}

int16_t INAFamilySensor::getCurrentMa()
{
    InaReading reading;
    if (!readChannel(batteryChannel, reading))
        return 0;
    return lround(reading.currentMa);
}

#endif
