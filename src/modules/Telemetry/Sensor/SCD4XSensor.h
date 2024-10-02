#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <SensirionI2CScd4x.h>

class SCD4XSensor : public TelemetrySensor
{
  private:
    SensirionI2CScd4x scd4x = SensirionI2CScd4x();

  protected:
    virtual void setup() override;

  public:
    SCD4XSensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif
