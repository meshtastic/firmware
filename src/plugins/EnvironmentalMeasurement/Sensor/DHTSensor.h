#include "../mesh/generated/environmental_measurement.pb.h"
#include "EnvironmentalMeasurementSensor.h"
#include <DHT.h>

#define DHT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS 1000

class DHTSensor : virtual public EnvironmentalMeasurementSensor {
private:
    DHT *dht = NULL;

public:
    DHTSensor();
    virtual int32_t runOnce() override;
    virtual bool getMeasurement(EnvironmentalMeasurement *measurement) override;
};    