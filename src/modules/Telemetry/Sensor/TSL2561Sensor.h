#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_TSL2561_U.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_TSL2561_U.h>

class TSL2561Sensor : public TelemetrySensor
{
  private:
    // The magic number is a sensor id, the actual value doesn't matter
    Adafruit_TSL2561_Unified tsl = Adafruit_TSL2561_Unified(TSL2561_ADDR_LOW, 12345);

  protected:
    virtual void setup() override;

  public:
    TSL2561Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};
#endif
