#include "../mesh/generated/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <DS18B20.h>
#include <OneWire.h>

class DallasSensor : virtual public TelemetrySensor {
private:
    OneWire *oneWire = NULL;
    DS18B20 *ds18b20 = NULL;

public:
    DallasSensor();
    virtual int32_t runOnce() override;
    virtual bool getMeasurement(Telemetry *measurement) override;
};    