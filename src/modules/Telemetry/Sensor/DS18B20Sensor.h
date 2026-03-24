#pragma once
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<DallasTemperature.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <DallasTemperature.h>
#include <OneWire.h>
#include <algorithm>
#include <vector>

#define MAX_DS18B20_SENSORS 16

class DS18B20Sensor : public TelemetrySensor
{
  private:
    OneWire *oneWire = nullptr;
    DallasTemperature *dallas = nullptr;
    uint8_t configuredPin = 0;
    bool conversionPending = false;

    struct SensorReading {
        uint8_t addr[8]; // 64-bit ROM code (DeviceAddress layout)
        float temperature;
    };
    std::vector<SensorReading> sensorCache;

    void scanBus();

  protected:
    virtual void setup() override {}

  public:
    DS18B20Sensor();
    bool hasSensor() { return true; }
    int getSensorCount() const { return (int)sensorCache.size(); }
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    bool getMetricsChunk(meshtastic_Telemetry *measurement, int offset);
};

#endif // __has_include(<DallasTemperature.h>)
