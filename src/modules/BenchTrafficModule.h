#pragma once

// Bench: -DBENCH_TRAFFIC_LOAD_PCT=<n> makes this node a background traffic source. It broadcasts frames of random
// length at exponentially distributed intervals, sized so they fill n% of airtime on average, with no relaying.
#ifdef BENCH_TRAFFIC_LOAD_PCT

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"

class BenchTrafficModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    BenchTrafficModule();

  protected:
    int32_t runOnce() override;

  private:
    uint32_t sent = 0;
    uint32_t skippedDutyCycle = 0;
    uint32_t offeredAirMs = 0;
    uint32_t firstSendMs = 0;
    uint32_t lastSummaryMs = 0;

    /// Queue one frame and return its expected airtime in ms, or 0 if nothing was sent
    uint32_t sendOne();
    /// An exponential draw with this mean, so arrivals are Poisson
    static uint32_t exponentialMs(uint32_t meanMs);
};

extern BenchTrafficModule *benchTrafficModule;

#endif
