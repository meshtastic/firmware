#include "../mesh/generated/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <DHT.h>

#define DHT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS 1000

class DHTSensor : virtual public TelemetrySensor {
private:
    DHT *dht = NULL;

public:
    DHTSensor();
    virtual int32_t runOnce() override;
    virtual bool getMeasurement(Telemetry *measurement) override;
};    