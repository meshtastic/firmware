#pragma once
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "NodeDB.h"

#define DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS 1000
extern uint8_t nodeTelemetrySensorsMap[_meshtastic_TelemetrySensorType_MAX + 1];

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
    meshtastic_TelemetrySensorType sensorType;
    unsigned status;
    bool initialized = false;

    int32_t initI2CSensor()
    {
        if (!status) {
            LOG_WARN("Could not connect to detected %s sensor.\n Removing from nodeTelemetrySensorsMap.\n", sensorName);
            nodeTelemetrySensorsMap[sensorType] = 0;
        } else {
            LOG_INFO("Opened %s sensor on default i2c bus\n", sensorName);
            setup();
        }
        initialized = true;
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    virtual void setup();

  public:
    bool hasSensor() { return sensorType < sizeof(nodeTelemetrySensorsMap) && nodeTelemetrySensorsMap[sensorType] > 0; }

    virtual int32_t runOnce() = 0;
    virtual bool isInitialized() { return initialized; }
    virtual bool isRunning() { return status > 0; }

    virtual bool getMetrics(meshtastic_Telemetry *measurement) = 0;
};