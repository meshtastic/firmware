#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Unit_Sonic.h>

class RCWL9620Sensor : public TelemetrySensor
{
  private:
    SONIC_I2C rcwl9620;

  protected:
    virtual void setup() override;

  public:
    RCWL9620Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};