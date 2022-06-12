#include "../mesh/generated/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_BME680.h>

class BME680Sensor : virtual public TelemetrySensor {
private:
    Adafruit_BME680 bme680;

protected:
    virtual void setup() override;
    
public:
    BME680Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(Telemetry *measurement) override;
};    