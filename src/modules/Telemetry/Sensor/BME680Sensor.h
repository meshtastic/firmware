#include "../mesh/generated/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_BME680.h>

#define BME_680_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS 1000

class BME680Sensor : virtual public TelemetrySensor {
private:
    Adafruit_BME680 bme680;

public:
    BME680Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMeasurement(Telemetry *measurement) override;
};    