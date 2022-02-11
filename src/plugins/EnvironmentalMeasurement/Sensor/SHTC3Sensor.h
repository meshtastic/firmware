#include "../mesh/generated/environmental_measurement.pb.h"
#include "EnvironmentalMeasurementSensor.h"
#include <SparkFun_SHTC3.h>

#define MCP_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS 1000

class SHTC3Sensor : virtual public EnvironmentalMeasurementSensor {

private:
    SHTC3 g_shtc3;

public:
    SHTC3Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMeasurement(EnvironmentalMeasurement *measurement) override;
};
