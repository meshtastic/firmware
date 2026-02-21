#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_VL53L0X.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_VL53L0X.h>

class VL53L0XSensor : public TelemetrySensor
{
  private:

    Adafruit_VL53L0X vl53l0x;

  public:
    VL53L0XSensor();
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
};
#endif
