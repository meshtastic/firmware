#include "../mesh/generated/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_INA219.h>


class INA219Sensor : virtual public TelemetrySensor {
private:
    Adafruit_INA219 ina219;

protected:
    virtual void setup() override;
    
public:
    INA219Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(Telemetry *measurement) override;
};    