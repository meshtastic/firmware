#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <SensirionI2cSht4x.h>

class SHT4XSensor : public TelemetrySensor
{
  private:
    SensirionI2cSht4x sht4x;

  protected:
    virtual void setup() override;

  public:
    SHT4XSensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif