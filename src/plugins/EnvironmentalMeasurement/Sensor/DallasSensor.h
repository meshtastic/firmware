#include "../mesh/generated/environmental_measurement.pb.h"
#include "EnvironmentalMeasurementSensor.h"
#include <DS18B20.h>
#include <OneWire.h>

#define DS18B20_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS 1000

class DallasSensor : virtual public EnvironmentalMeasurementSensor {
private:
    OneWire *oneWire = NULL;
    DS18B20 *ds18b20 = NULL;

public:
    DallasSensor();
    virtual int32_t runOnce() override;
    virtual bool getMeasurement(EnvironmentalMeasurement *measurement) override;
};    