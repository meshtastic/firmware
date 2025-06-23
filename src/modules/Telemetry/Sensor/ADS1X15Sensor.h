#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_ADS1015.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "VoltageSensor.h"
#include <Adafruit_ADS1015.h>

class ADS1X15Sensor : public TelemetrySensor, VoltageSensor
{
  private:
    Adafruit_ADS1015 ads1x15;

  protected:
    virtual void setup() override;

  public:
    ADS1X15Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif