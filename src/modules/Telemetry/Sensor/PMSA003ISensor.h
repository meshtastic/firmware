#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR

#include "../detect/ReClockI2C.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "gps/RTC.h"

#ifndef PMSA003I_I2C_CLOCK_SPEED
#define PMSA003I_I2C_CLOCK_SPEED 100000
#endif

#ifndef PMSA003I_FRAME_LENGTH
#define PMSA003I_FRAME_LENGTH 32
#endif

#ifndef PMSA003I_WARMUP_MS
#define PMSA003I_WARMUP_MS 30000
#endif

class PMSA003ISensor : public TelemetrySensor
{
  public:
    PMSA003ISensor();
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;

    virtual bool isActive() override;
    virtual void sleep() override;
    virtual uint32_t wakeUp() override;
    virtual bool canSleep() override;
    virtual int32_t wakeUpTimeMs() override;
    virtual int32_t pendingForReadyMs() override;

  private:
    enum PMSA003IState { PMSA003I_IDLE, PMSA003I_ACTIVE };
    PMSA003IState state = PMSA003I_ACTIVE;

    uint16_t computedChecksum = 0;
    uint16_t receivedChecksum = 0;
    uint32_t pmMeasureStarted = 0;

    uint8_t buffer[PMSA003I_FRAME_LENGTH]{};
#ifdef PMSA003I_I2C_CLOCK_SPEED
    ReClockI2C reClockI2C;
#endif
};

#endif
