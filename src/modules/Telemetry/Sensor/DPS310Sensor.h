#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_DPS310.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_DPS310.h>

class DPS310Sensor : public TelemetrySensor
{
  private:
    Adafruit_DPS310 dps310;

  protected:
    virtual void setup() override;

  public:
    DPS310Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif