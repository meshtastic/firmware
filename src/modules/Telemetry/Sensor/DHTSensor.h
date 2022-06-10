#include "../mesh/generated/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <DHT.h>

class DHTSensor : virtual public TelemetrySensor {
private:
    DHT *dht = NULL;

protected:
    virtual void setup() override;

public:
    DHTSensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(Telemetry *measurement) override;
};    