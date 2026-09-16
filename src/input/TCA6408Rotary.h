#pragma once

#include "InputBroker.h"
#include "concurrency/OSThread.h"
#include "configuration.h"

#if defined(HAS_TCA6408_ROTARY)

/**
 * Rotary encoder on the A/B inputs of a TCA6408 I2C GPIO expander. SENSOR_INT goes low on any
 * input change; the interrupt only wakes this thread, so all I2C and decoder state stay on it.
 */
class TCA6408Rotary : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    explicit TCA6408Rotary(const char *name);
    bool init();
    int32_t runOnce() override;

  private:
    static void interruptHandler();
    void powerSensorBus();
    bool writeRegister(uint8_t reg, uint8_t value);
    bool readInput(uint8_t &value);
    void handleTransition(uint8_t newState);

    const char *_originName;
    uint8_t inputState;
    uint32_t lastEventMs = 0;
    bool ready = false;
    // Latched between the first falling edge of a detent and both inputs returning high,
    // so one detent reports exactly one event.
    bool activeLowPhase = false;

    static TCA6408Rotary *instance;
};

extern TCA6408Rotary *tca6408Rotary;

#endif
