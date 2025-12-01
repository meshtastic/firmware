#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_LPS2X.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_LPS2X.h>
#include <Adafruit_Sensor.h>

class LPS22HBSensor : public TelemetrySensor
{
  private:
    Adafruit_LPS22 lps22hb;

  public:
    LPS22HBSensor();
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
};

#endif