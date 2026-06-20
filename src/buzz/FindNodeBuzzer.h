#pragma once

#include "concurrency/OSThread.h"
#include <stdint.h>

class FindNodeBuzzer : private concurrency::OSThread
{
  public:
    enum class Result : uint8_t { Started, Stopped, NoBuzzer, BuzzerDisabled };

    static constexpr uint32_t DEFAULT_DURATION_SECONDS = 30;
    static constexpr uint32_t MAX_DURATION_SECONDS = 300;

    FindNodeBuzzer();

    Result start(uint32_t durationSeconds, uint32_t *acceptedDurationSeconds = nullptr);
    Result stop();
    bool isActive() const;

  private:
    uint32_t startMsec = 0;
    uint32_t durationMsec = 0;

    int32_t runOnce() override;
};

extern FindNodeBuzzer *findNodeBuzzer;
