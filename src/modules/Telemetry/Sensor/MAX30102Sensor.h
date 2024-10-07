#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && !defined(ARCH_PORTDUINO)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <MAX30105.h>

#define MAX30102_BUFFER_LEN 100

class MAX30102Sensor : public TelemetrySensor
{
  private:
    MAX30105 max30102 = MAX30105();
    uint32_t _speed = 200000UL;

  protected:
    virtual void setup() override;

  public:
    MAX30102Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif
