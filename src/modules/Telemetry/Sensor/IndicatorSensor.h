#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/interdevice.pb.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <RingBuf.h>

class IndicatorSensor : public TelemetrySensor
{
  protected:
    virtual void setup() override;

  public:
    IndicatorSensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    size_t stuff_buffer(meshtastic_SensorData message);

  private:
    RingBuf<meshtastic_SensorData, 16> ringBuf;
};

#endif