#include "../mesh/generated/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_BME280.h>

class BME280Sensor : virtual public TelemetrySensor {
private:
    Adafruit_BME280 bme280;

protected:
    virtual void setup() override;

public:
    BME280Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(Telemetry *measurement) override;
};    