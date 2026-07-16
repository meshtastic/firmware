#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR && __has_include(<Seeed_HM330X.h>)

#include "../detect/reClockI2C.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "RTC.h"
#include "TelemetrySensor.h"
#include <Seeed_HM330X.h>

#define HM330X_I2C_CLOCK_SPEED 400000
#define HM330X_WARMUP_MS 30000
#define HM330X_NO_ERROR 0
#define HM330X_FRAME_LENGTH 30

class HM330XSensor : public TelemetrySensor
{
  private:
    enum class State { IDLE, ACTIVE };
    State state = State::IDLE;
    uint32_t measureStarted = 0;
    uint8_t buffer[HM330X_FRAME_LENGTH]{};
    TwoWire *_bus{};
    uint8_t _address{};

    HM330X hm330x;
    HM330XErrorCode checksum(uint8_t* data);

  public:
    HM330XSensor();
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;

    virtual bool isActive() override;
    // virtual void sleep() override; // no need for this on this sensor
    virtual uint32_t wakeUp() override;
    virtual bool canSleep() override;
    virtual int32_t wakeUpTimeMs() override;
    virtual int32_t pendingForReadyMs() override;
};

#endif
