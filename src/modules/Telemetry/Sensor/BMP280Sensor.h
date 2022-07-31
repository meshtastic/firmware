#include "../mesh/generated/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_BMP280.h>

class BMP280Sensor : virtual public TelemetrySensor {
private:
    Adafruit_BMP280 bmp280;

protected:
    virtual void setup() override;

public:
    BMP280Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(Telemetry *measurement) override;
};    