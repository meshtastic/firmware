#pragma once
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && !MESHTASTIC_EXCLUDE_ACCELEROMETER && !defined(ARCH_STM32WL)

#include "../concurrency/OSThread.h"

class TiltTelemetryModule : public concurrency::OSThread
{
  public:
    TiltTelemetryModule();

  protected:
    int32_t runOnce() override;

  private:
    static constexpr float    THRESHOLD_DEG  = 0.5f;            // send on tilt change >= 0.5°
    static constexpr uint32_t POLL_MS        = 2000;            // check every 2s
    static constexpr uint32_t HEARTBEAT_MS   = 5 * 60 * 1000;  // send every 5 min regardless

    float    _lastRoll    = 0.0f;
    float    _lastPitch   = 0.0f;
    uint32_t _lastSentMs  = 0;
    bool     _firstSend   = true;
};

extern TiltTelemetryModule *tiltTelemetryModule;

#endif
