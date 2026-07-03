#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_SPA06_003.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_SPA06_003.h>

class SPA06Sensor : public TelemetrySensor
{
  private:
    Adafruit_SPA06_003 spa06;
    Adafruit_Sensor *spa_temp;
    Adafruit_Sensor *spa_pressure;

  public:
    SPA06Sensor();
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
};

#endif
