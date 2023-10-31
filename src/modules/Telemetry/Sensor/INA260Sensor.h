#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "VoltageSensor.h"
#include <Adafruit_INA260.h>

class INA260Sensor : public TelemetrySensor, VoltageSensor
{
  private:
    Adafruit_INA260 ina260 = Adafruit_INA260();

  protected:
    virtual void setup() override;

  public:
    INA260Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual uint16_t getBusVoltageMv() override;
};