#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_BMP085.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_BMP085.h>

class BMP085Sensor : public TelemetrySensor
{
  private:
    Adafruit_BMP085 bmp085;

  protected:
    virtual void setup() override;

  public:
    BMP085Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif