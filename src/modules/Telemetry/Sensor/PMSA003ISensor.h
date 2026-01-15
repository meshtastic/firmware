#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"

#define PMSA003I_I2C_CLOCK_SPEED 100000
#define PMSA003I_FRAME_LENGTH 32
#define PMSA003I_WARMUP_MS 30000

class PMSA003ISensor : public TelemetrySensor
{
  public:
    PMSA003ISensor();
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;

    virtual bool isActive() override;
    virtual void sleep() override;
    virtual uint32_t wakeUp() override;

  private:
    enum class State { IDLE, ACTIVE };
    State state = State::ACTIVE;

    uint16_t computedChecksum = 0;
    uint16_t receivedChecksum = 0;

    uint8_t buffer[PMSA003I_FRAME_LENGTH]{};
    TwoWire *_bus{};
    uint8_t _address{};
};

#endif