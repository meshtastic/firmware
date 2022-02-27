#include "../mesh/generated/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_BME280.h>

#define BME_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS 1000

class BME280Sensor : virtual public TelemetrySensor {
private:
    Adafruit_BME280 bme280;

public:
    BME280Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMeasurement(Telemetry *measurement) override;
};    