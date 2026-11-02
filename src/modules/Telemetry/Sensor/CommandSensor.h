#pragma once

#include "configuration.h"

#if ARCH_PORTDUINO && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <vector>

// Fills EnvironmentMetrics fields from the shell commands listed under Sensors: in config.yaml.
class CommandSensor : public TelemetrySensor
{
  public:
    CommandSensor();
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;

  private:
    struct Reading {
        bool valid = false;
        float value = 0;
        uint32_t readAtMs = 0; // 0 = never read
    };
    // Parallel to portduino_config.command_sensors.
    std::vector<Reading> readings;

    void refresh(size_t index);
};

#endif
