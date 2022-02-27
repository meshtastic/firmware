#pragma once
#include "../mesh/generated/telemetry.pb.h"
#define DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS 1000

class TelemetrySensor {
protected:
    TelemetrySensor() { }

public:
    virtual int32_t runOnce() = 0;
    virtual bool getMeasurement(Telemetry *measurement)  = 0;
};
