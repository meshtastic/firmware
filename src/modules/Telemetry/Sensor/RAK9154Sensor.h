#ifdef HAS_RAKPROT
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "VoltageSensor.h"

class RAK9154Sensor : public TelemetrySensor, VoltageSensor
{
  private:
  protected:
    virtual void setup() override;

  public:
    RAK9154Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual uint16_t getBusVoltageMv() override;
    int getBusBatteryPercent();
    bool isCharging();
};
#endif // HAS_RAKPROT