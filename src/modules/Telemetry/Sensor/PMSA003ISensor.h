#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_PM25AQI.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "detect/ScanI2CTwoWire.h"
#include <Adafruit_PM25AQI.h>

#ifndef PMSA003I_WARMUP_MS
// from the PMSA003I datasheet:
// "Stable data should be got at least 30 seconds after the sensor wakeup
// from the sleep mode because of the fan’s performance."
#define PMSA003I_WARMUP_MS 30000
#endif

class PMSA003ISensor : public TelemetrySensor
{
  private:
    Adafruit_PM25AQI pmsa003i = Adafruit_PM25AQI();
    PM25_AQI_Data pmsa003iData = {0};

  protected:
    virtual void setup() override;

  public:
    enum State {
        IDLE = 0,
        ACTIVE = 1,
    };

#ifdef PMSA003I_ENABLE_PIN
    void sleep();
    uint32_t wakeUp();
    // the PMSA003I sensor uses about 300mW on its own; support powering it off when it's not actively taking
    // a reading
    // put the sensor to sleep on startup
    State state = State::IDLE;
#else
    State state = State::ACTIVE;
#endif

    PMSA003ISensor();
    bool isActive();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
};

#endif