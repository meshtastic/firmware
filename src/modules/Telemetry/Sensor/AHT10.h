#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_AHTX0.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_AHTX0.h>

class AHT10Sensor : public TelemetrySensor
{
  private:
    Adafruit_AHTX0 aht10;

  protected:
    virtual void setup() override;

  public:
    AHT10Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif