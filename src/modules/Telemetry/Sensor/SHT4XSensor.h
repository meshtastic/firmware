#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_SHT4x.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_SHT4x.h>

class SHT4XSensor : public TelemetrySensor
{
  private:
    Adafruit_SHT4x sht4x = Adafruit_SHT4x();

  protected:
    virtual void setup() override;

  public:
    SHT4XSensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif