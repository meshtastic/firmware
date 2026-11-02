#include "configuration.h"

#if ARCH_PORTDUINO && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "CommandSensor.h"
#include "CommandSensorFields.h"
#include "PortduinoGlue.h"
#include "UptimeClock.h"
#include "mesh/Throttle.h"
#include <cmath>

// getMetrics() also runs for screen refreshes and phone requests; this keeps those from re-running every command.
static constexpr uint32_t COMMAND_SENSOR_CACHE_MS = 30 * 1000;

CommandSensor::CommandSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SENSOR_UNSET, "Command") {}

bool CommandSensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    if (portduino_config.command_sensors.empty())
        return false;

    readings.assign(portduino_config.command_sensors.size(), Reading());
    LOG_INFO("Init sensor: %s, %u from config.yaml Sensors", sensorName,
             static_cast<unsigned>(portduino_config.command_sensors.size()));
    status = 1;
    initialized = true;
    return true;
}

void CommandSensor::refresh(size_t index)
{
    const commandSensorConfig &sensor = portduino_config.command_sensors[index];
    Reading &reading = readings[index];
    reading.readAtMs = Time::stampMillis();
    reading.valid = false;

    std::string output;
    if (!execCommand(sensor.command.c_str(), COMMAND_SENSOR_TIMEOUT_MS, output)) {
        LOG_WARN("Sensor %s: command failed or timed out: %s", sensor.type.c_str(), sensor.command.c_str());
        return;
    }
    float value;
    if (!CommandSensorFields::parseValue(output, value)) {
        while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
            output.pop_back();
        LOG_WARN("Sensor %s: command output is not a number: '%s'", sensor.type.c_str(), output.c_str());
        return;
    }
    const float scaled = value * sensor.scale;
    if (!std::isfinite(scaled)) {
        LOG_WARN("Sensor %s: %f scaled by %f is not a finite number", sensor.type.c_str(), value, sensor.scale);
        return;
    }
    reading.value = scaled;
    reading.valid = true;
    LOG_DEBUG("Sensor %s: %f", sensor.type.c_str(), reading.value);
}

bool CommandSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    bool any = false;
    for (size_t i = 0; i < readings.size(); i++) {
        if (readings[i].readAtMs == 0 || Throttle::hasElapsed(readings[i].readAtMs, COMMAND_SENSOR_CACHE_MS))
            refresh(i);
        if (readings[i].valid && CommandSensorFields::apply(measurement->variant.environment_metrics,
                                                            portduino_config.command_sensors[i].type, readings[i].value))
            any = true;
    }
    return any;
}

#endif
