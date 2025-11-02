/*
 *  Worth noting that both the AHT10 and AHT20 are supported without alteration.
 */

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_AHTX0.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_AHTX0.h>

class AHT10Sensor : public TelemetrySensor
{
  private:
    Adafruit_AHTX0 aht10;

  public:
    AHT10Sensor();
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
};

#endif
