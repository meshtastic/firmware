#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_SHT31.h>

class SHT31Sensor : public TelemetrySensor
{
  private:
    Adafruit_SHT31 sht31;

  protected:
    virtual void setup() override;

  public:
    SHT31Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif