#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_PM25AQI.h>

class PMSA0031Sensor : public TelemetrySensor
{
  private:
    Adafruit_PM25AQI aqi;
    PM25_AQI_Data data = {0};

  protected:
    virtual void setup() override;

  public:
    PMSA0031Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif