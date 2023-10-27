#include "TelemetrySensor.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include <INA3221.h>

class INA3221Sensor : public TelemetrySensor {
public:
    INA3221Sensor();
    int32_t runOnce() override;
    void setup() override;
    bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual uint16_t getBusVoltageMv();

private:
    INA3221 ina3221 = INA3221(INA3221_ADDR42_SDA);
};