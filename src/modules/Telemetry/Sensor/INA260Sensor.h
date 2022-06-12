#include "../mesh/generated/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_INA260.h>


class INA260Sensor : virtual public TelemetrySensor {
private:
    Adafruit_INA260 ina260 = Adafruit_INA260();

protected:
    virtual void setup() override;
    
public:
    INA260Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(Telemetry *measurement) override;
};    