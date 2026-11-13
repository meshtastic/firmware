#pragma once

#include "configuration.h"

#if ARCH_PORTDUINO && (!MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR || !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "CommandSensorFields.h"
#include "PortduinoGlue.h"
#include "TelemetrySensor.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Fills telemetry fields from the shell commands listed under Sensors: in config.yaml. Each telemetry module
// owns one instance, which handles only the entries whose Type belongs to that module's message.
// Commands run on a worker thread, so a slow or hung one never stalls the main loop; getMetrics() only
// reads the latest results.
class CommandSensor : public TelemetrySensor
{
  public:
    explicit CommandSensor(CommandSensorFields::Message message);
    ~CommandSensor() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;

    // Commands the worker has finished running, successful or not. For tests and diagnostics.
    uint32_t finishedReads();

  private:
    struct Reading {
        bool valid = false;
        float value = 0;
        uint32_t readAtMs = 0;   // 0 = never read
        uint32_t generation = 0; // bumped on every finished read
        std::string problem;     // why the last read gave no value; empty when valid
    };

    const CommandSensorFields::Message message;
    std::vector<commandSensorConfig> configs; // this module's entries, copied at init; read-only after
    std::mutex mu;
    std::vector<Reading> readings;           // guarded by mu
    std::vector<uint32_t> loggedGenerations; // main thread only
    std::atomic<bool> stopping{false};
    std::thread worker;

    void run();
    Reading read(const commandSensorConfig &sensor);
};

#endif
