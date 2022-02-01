#pragma once
#include "../mesh/generated/environmental_measurement.pb.h"
#define DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS 1000

class EnvironmentalMeasurementSensor {
protected:
    EnvironmentalMeasurementSensor() { }

public:
    virtual int32_t runOnce() = 0;
    virtual bool getMeasurement(EnvironmentalMeasurement *measurement)  = 0;
};
