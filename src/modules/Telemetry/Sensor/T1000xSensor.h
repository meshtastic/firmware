#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"

class T1000xSensor : public TelemetrySensor
{
  protected:
    virtual void setup() override;

  public:
    T1000xSensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual float getLux();
    virtual float getTemp();
};

#endif