#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<SensirionI2CSen5x.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <SensirionI2CSen5x.h>

#ifndef SEN5X_WARMUP_MS
// from the SEN5X datasheet
#define SEN5X_WARMUP_MS_SMALL 30000
#endif

class SEN5XSensor : public TelemetrySensor
{
  private:
    SensirionI2CSen5x sen5x;
    // PM25_AQI_Data pmsa003iData = {0};

  protected:
    virtual void setup() override;

  public:
    enum State {
        IDLE = 0,
        ACTIVE = 1,
    };

#ifdef SEN5X_ENABLE_PIN
    void sleep();
    uint32_t wakeUp();
    State state = State::IDLE;
#else
    State state = State::ACTIVE;
#endif

    SEN5XSensor();
    bool isActive();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif