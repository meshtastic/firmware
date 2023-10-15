#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_SHTC3.h>

class SHTC3Sensor : public TelemetrySensor
{
  private:
    Adafruit_SHTC3 shtc3 = Adafruit_SHTC3();

  protected:
    virtual void setup() override;

  public:
    SHTC3Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};