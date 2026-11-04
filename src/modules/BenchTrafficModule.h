#pragma once

// Bench: -DBENCH_TRAFFIC_LOAD_PCT=<n> makes this node a traffic source. It broadcasts frames of random length, sized so
// they fill n% of airtime on average, with no relaying, and logs every such frame it hears from another node.
#ifdef BENCH_TRAFFIC_LOAD_PCT

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"

class BenchTrafficModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    BenchTrafficModule();

  protected:
    int32_t runOnce() override;
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

  private:
    uint32_t sent = 0;
    uint32_t skippedDutyCycle = 0;
    uint32_t offeredAirMs = 0;
    uint32_t firstSendMs = 0;
    uint32_t lastSummaryMs = 0;
    uint32_t nextDueMs = 0;     // arrivals are scheduled on this timeline, not after each send, so send time adds no gap
    uint32_t sendBlockedMs = 0; // total time sendOne() held the loop
    uint32_t lateMs = 0;        // total time runOnce() started after its due time, i.e. other work held the loop
    uint32_t reanchors = 0;     // times the schedule fell too far behind and restarted from now
    uint32_t rejected = 0;      // frames the router refused (a full TX queue), counted apart from sent
    uint32_t heard = 0;         // another node's frames received
    bool done = false;          // BENCH_TRAFFIC_COUNT frames sent and the final summary logged

    /// Queue one frame and return its expected airtime in ms, or 0 if nothing was sent
    uint32_t sendOne();
    /// An exponential draw with this mean, so arrivals are Poisson
    static uint32_t exponentialMs(uint32_t meanMs);
    /// The gap to the next arrival: exponential, or the mean +- BENCH_TRAFFIC_JITTER_PCT uniformly when that is set
    static uint32_t gapMs(uint32_t meanMs);
    void logSummary(uint32_t now, const char *what);
};

extern BenchTrafficModule *benchTrafficModule;

#endif
