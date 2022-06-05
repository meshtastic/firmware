#pragma once
#include "../mesh/generated/telemetry.pb.h"
#include "NodeDB.h"
#include "main.h"
#include <map>

#define DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS 1000

inline bool hasSensor(TelemetrySensorType sensorType) { 
  return sensorType < sizeof(nodeTelemetrySensorsMap) && nodeTelemetrySensorsMap[sensorType] > 0;
}

class TelemetrySensor
{
  protected:
    TelemetrySensor() {}

  public:
    virtual int32_t runOnce() = 0;
    virtual bool getMeasurement(Telemetry *measurement) = 0;
};
