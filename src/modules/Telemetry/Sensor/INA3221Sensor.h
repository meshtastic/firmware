#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "VoltageSensor.h"
#include <INA3221.h>

class INA3221Sensor : public TelemetrySensor, VoltageSensor
{
  private:
    INA3221 ina3221 = INA3221(INA3221_ADDR42_SDA);

  protected:
    void setup() override;

  public:
    INA3221Sensor();
    int32_t runOnce() override;
    bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual uint16_t getBusVoltageMv() override;
};