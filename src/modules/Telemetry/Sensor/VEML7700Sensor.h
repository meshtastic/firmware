#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_VEML7700.h>

class VEML7700Sensor : public TelemetrySensor
{
  private:
    Adafruit_VEML7700 veml7700;

  protected:
    virtual void setup() override;

  public:
    VEML7700Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};