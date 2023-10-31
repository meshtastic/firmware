#pragma once
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "NodeDB.h"
#include <utility>

class TwoWire;

#define DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS 1000
extern std::pair<uint8_t, TwoWire *> nodeTelemetrySensorsMap[_meshtastic_TelemetrySensorType_MAX + 1];

class TelemetrySensor
{
  protected:
    TelemetrySensor(meshtastic_TelemetrySensorType sensorType, const char *sensorName)
    {
        this->sensorName = sensorName;
        this->sensorType = sensorType;
        this->status = 0;
    }

    const char *sensorName;
    meshtastic_TelemetrySensorType sensorType = meshtastic_TelemetrySensorType_SENSOR_UNSET;
    unsigned status;
    bool initialized = false;

    int32_t initI2CSensor()
    {
        if (!status) {
            LOG_WARN("Could not connect to detected %s sensor.\n Removing from nodeTelemetrySensorsMap.\n", sensorName);
            nodeTelemetrySensorsMap[sensorType].first = 0;
        } else {
            LOG_INFO("Opened %s sensor on i2c bus\n", sensorName);
            setup();
        }
        initialized = true;
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    virtual void setup();

  public:
    bool hasSensor() { return nodeTelemetrySensorsMap[sensorType].first > 0; }

    virtual int32_t runOnce() = 0;
    virtual bool isInitialized() { return initialized; }
    virtual bool isRunning() { return status > 0; }

    virtual bool getMetrics(meshtastic_Telemetry *measurement) = 0;
};