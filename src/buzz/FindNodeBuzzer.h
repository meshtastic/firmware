#pragma once

#include "concurrency/OSThread.h"
#include <stdint.h>

class FindNodeBuzzer : private concurrency::OSThread
{
  public:
    enum class Result : uint8_t { Started, Stopped, NoBuzzer, BuzzerDisabled };

    static constexpr uint32_t DEFAULT_DURATION_SECONDS = 5 * 60;

    FindNodeBuzzer();

    Result start();
    Result stop();
    bool isActive() const;

  private:
    uint32_t startMsec = 0;
    uint32_t durationMsec = 0;

    int32_t runOnce() override;
};

extern FindNodeBuzzer *findNodeBuzzer;
