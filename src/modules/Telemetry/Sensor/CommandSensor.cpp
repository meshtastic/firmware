#include "configuration.h"

#if ARCH_PORTDUINO && (!MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR || !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR)

#include "CommandSensor.h"
#include "UptimeClock.h"
#include "mesh/Throttle.h"
#include <chrono>
#include <cmath>

// How often each command is re-run.
static constexpr uint32_t COMMAND_SENSOR_REFRESH_MS = 30 * 1000;
// How often the worker wakes to check for due commands and for shutdown.
static constexpr uint32_t COMMAND_SENSOR_POLL_MS = 100;
static constexpr size_t COMMAND_SENSOR_MAX_QUOTED_OUTPUT = 64;

CommandSensor::CommandSensor(CommandSensorFields::Message message)
    : TelemetrySensor(meshtastic_TelemetrySensorType_SENSOR_UNSET, "Command"), message(message)
{
}

CommandSensor::~CommandSensor()
{
    stopping = true;
    if (worker.joinable())
        worker.join();
}

bool CommandSensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    for (const auto &sensor : portduino_config.command_sensors)
        if (CommandSensorFields::messageFor(sensor.type) == message)
            configs.push_back(sensor);
    if (configs.empty())
        return false;

    readings.assign(configs.size(), Reading());
    loggedGenerations.assign(configs.size(), 0);
    LOG_INFO("Init sensor: %s, %u %s entries from config.yaml Sensors", sensorName, static_cast<unsigned>(configs.size()),
             message == CommandSensorFields::Message::AirQuality ? "air quality" : "environment");
    status = 1;
    initialized = true;
    worker = std::thread(&CommandSensor::run, this);
    return true;
}

// Worker thread. Must not call LOG_*: portduino leaves RedirectablePrint's buffer unlocked, so results
// and problems are handed to getMetrics() to log on the main thread.
void CommandSensor::run()
{
    while (!stopping) {
        for (size_t i = 0; i < configs.size() && !stopping; i++) {
            uint32_t readAtMs;
            {
                std::lock_guard<std::mutex> lock(mu);
                readAtMs = readings[i].readAtMs;
            }
            if (readAtMs != 0 && !Throttle::hasElapsed(readAtMs, COMMAND_SENSOR_REFRESH_MS))
                continue;

            Reading fresh = read(configs[i]);
            std::lock_guard<std::mutex> lock(mu);
            fresh.generation = readings[i].generation + 1;
            readings[i] = fresh;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(COMMAND_SENSOR_POLL_MS));
    }
}

CommandSensor::Reading CommandSensor::read(const commandSensorConfig &sensor)
{
    Reading reading;
    reading.readAtMs = Time::stampMillis();

    std::string output;
    switch (execCommand(sensor.command.c_str(), COMMAND_SENSOR_TIMEOUT_MS, output)) {
    case ExecResult::Ok:
        break;
    case ExecResult::TimedOut:
        reading.problem = "timed out after " + std::to_string(COMMAND_SENSOR_TIMEOUT_MS) + "ms and was killed";
        return reading;
    case ExecResult::NotStarted:
        reading.problem = "could not be started";
        return reading;
    default:
        reading.problem = "exited with an error";
        return reading;
    }

    float value;
    if (!CommandSensorFields::parseValue(output, value)) {
        while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
            output.pop_back();
        if (output.size() > COMMAND_SENSOR_MAX_QUOTED_OUTPUT)
            output = output.substr(0, COMMAND_SENSOR_MAX_QUOTED_OUTPUT) + "...";
        reading.problem = "printed '" + output + "', which is not a number";
        return reading;
    }
    const float scaled = value * sensor.scale;
    if (!std::isfinite(scaled)) {
        reading.problem = "printed a value that is not a finite number once scaled";
        return reading;
    }
    reading.value = scaled;
    reading.valid = true;
    return reading;
}

bool CommandSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    bool any = false;
    std::lock_guard<std::mutex> lock(mu);
    for (size_t i = 0; i < readings.size(); i++) {
        const Reading &reading = readings[i];
        const commandSensorConfig &sensor = configs[i];
        if (reading.generation != loggedGenerations[i]) {
            loggedGenerations[i] = reading.generation;
            if (reading.valid)
                LOG_DEBUG("Sensor %s: %f", sensor.type.c_str(), reading.value);
            else
                LOG_WARN("Sensor %s: command %s: %s", sensor.type.c_str(), reading.problem.c_str(), sensor.command.c_str());
        }
        if (!reading.valid)
            continue;
        if (message == CommandSensorFields::Message::Environment &&
            measurement->which_variant == meshtastic_Telemetry_environment_metrics_tag)
            any = CommandSensorFields::apply(measurement->variant.environment_metrics, sensor.type, reading.value) || any;
        else if (message == CommandSensorFields::Message::AirQuality &&
                 measurement->which_variant == meshtastic_Telemetry_air_quality_metrics_tag)
            any = CommandSensorFields::apply(measurement->variant.air_quality_metrics, sensor.type, reading.value) || any;
    }
    return any;
}

uint32_t CommandSensor::finishedReads()
{
    std::lock_guard<std::mutex> lock(mu);
    uint32_t total = 0;
    for (const auto &reading : readings)
        total += reading.generation;
    return total;
}

#endif
