#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<SensirionI2cScd4x.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <SensirionI2cScd4x.h>

class SCD4XSensor : public TelemetrySensor
{
  private:
    SensirionI2cScd4x scd4x;

  protected:
    virtual void setup() override;

  public:
    SCD4XSensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif