#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR && __has_include(<SensirionI2cSfa3x.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "RTC.h"
#include "TelemetrySensor.h"
#include <SensirionI2cSfa3x.h>

#define SFA30_I2C_CLOCK_SPEED 100000
#define SFA30_WARMUP_MS 10000
#define SFA30_NO_ERROR 0

class SFA30Sensor : public TelemetrySensor
{
  private:
    enum class State { IDLE, ACTIVE };
    State state = State::IDLE;
    uint32_t measureStarted = 0;

    SensirionI2cSfa3x sfa30;
    TwoWire *_bus{};
    uint8_t _address{};
    bool isError(uint16_t response);

  public:
    SFA30Sensor();
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;

    virtual bool isActive() override;
    virtual void sleep() override;
    virtual uint32_t wakeUp() override;
    virtual bool canSleep() override;
    virtual int32_t wakeUpTimeMs() override;
    virtual int32_t pendingForReadyMs() override;
};

#endif
