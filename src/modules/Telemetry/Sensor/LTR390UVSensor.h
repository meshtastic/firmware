#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_LTR390.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_LTR390.h>

class LTR390UVSensor : public TelemetrySensor
{
  private:
    Adafruit_LTR390 ltr390uv = Adafruit_LTR390();
    float lastLuxReading = 0;
    float lastUVReading = 0;

  public:
    LTR390UVSensor();
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
};

#endif