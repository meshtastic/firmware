#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_LPS2X.h>
#include <Adafruit_Sensor.h>

class LPS22HBSensor : public TelemetrySensor
{
  private:
    Adafruit_LPS22 lps22hb;

  protected:
    virtual void setup() override;

  public:
    LPS22HBSensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};