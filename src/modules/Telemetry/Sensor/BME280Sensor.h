#include "../mesh/generated/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_BME280.h>

class BME280Sensor : virtual public TelemetrySensor {
private:
    Adafruit_BME280 bme280;

public:
    BME280Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMeasurement(Telemetry *measurement) override;
};    