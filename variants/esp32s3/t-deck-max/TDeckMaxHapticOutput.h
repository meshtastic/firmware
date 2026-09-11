#pragma once

#include "input/HapticOutput.h"
#include "concurrency/Lock.h"

#include <Adafruit_DRV2605.h>

class DevicePowerController;

class TDeckMaxHapticOutput final : public HapticOutput
{
  public:
    bool begin() override;
    bool isAvailable() const override { return ready; }
    void play(HapticEffect effect) override;
    void stop() override;
    void pulse(uint16_t durationMs) override;
    void armDelayedPulse(uint16_t delayMs, uint16_t durationMs) override;
    void cancelDelayedPulse() override;
    int32_t runOnce() override;

  private:
    void triggerLocked(HapticEffect effect);
    void stopDriverLocked();

    Adafruit_DRV2605 driver;
    concurrency::Lock driverLock;
    DevicePowerController *power = nullptr;
    bool ready = false;
    bool motorPowerOn = false;
    uint32_t motorPowerChangedAt = 0;
    uint32_t pulseStartedAt = 0;
    uint32_t delayedPulseStartedAt = 0;
    uint16_t pulseDuration = 0;
    uint16_t delayedPulseDelay = 0;
    uint16_t delayedPulseDuration = 0;
    bool pulseActive = false;
    bool delayedPulsePending = false;
};
