#pragma once
#include "../mesh/generated/telemetry.pb.h"
#include "NodeDB.h"
#define DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS 1000

class TelemetrySensor {
protected:
    TelemetrySensor() { }
    Config_ModuleConfig_TelemetryConfig moduleConfig = config.payloadVariant.module_config.payloadVariant.telemetry_config;
public:
    virtual int32_t runOnce() = 0;
    virtual bool getMeasurement(Telemetry *measurement)  = 0;
};
