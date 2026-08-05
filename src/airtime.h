#pragma once

#include "MeshRadio.h"
#include "concurrency/Lock.h"
#include "concurrency/LockGuard.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

/*
  AirTime records how long the radio was busy, and turns that into the two
  percentages the transmit gates and DeviceMetrics use.

  INPUTS - four events change this class's state, three of them the same call:

    logAirtime(TX_LOG, ms)      one per completed transmission, our own and
                                relayed. Reaches all three stores.
    logAirtime(RX_LOG, ms)      one per well-formed reception. The interface is
                                promiscuous, so this counts packets not
                                addressed to us and every duplicate relay copy.
    logAirtime(RX_ALL_LOG, ms)  one per reception we could NOT parse: failed
                                CRC, truncated, region unset, or a collision.
    elapsed time                read from Time::getUptimeSecs() by syncNow() on
                                every public entry point. The only input that
                                *removes* airtime.

  RX_LOG and RX_ALL_LOG are DISJOINT. Both radio drivers pick exactly one per
  packet (RadioLibInterface::handleReceiveInterrupt, SimRadio likewise), so
  RX_ALL_LOG is "airtime we could not parse", not a superset of RX_LOG. An
  earlier version of this comment claimed otherwise and offered
  "RX_ALL_LOG - RX_LOG = other lora radios", which is wrong twice over: the
  subtraction can go negative, and the total is TX + RX + RX_ALL.

  OUTPUTS:

    channelUtilizationPercent()  % of the last 60s the channel was busy, from
                                 all three types. Read per send attempt, per
                                 screen frame, and by telemetry.
    utilizationTXPercent()       % of the last hour WE transmitted. Feeds the
                                 duty-cycle checks.
    isTxAllowedChannelUtil()     gate on the above, at 40% or 25% "polite"
    isTxAllowedAirUtil()         gate at HALF the region's duty cycle
    getSilentMinutes()           minutes of silence until the TX figure drops
                                 back under a limit. Feeds a log line and a
                                 client notification only - it gates nothing.
    airtimeReport()              8 x 1h of raw ms per type, for the HTTP report
    getSecondsSinceBoot()        the clock the buckets are keyed to

  The three thresholds (40%, 25%, half the duty cycle) are hard-coded members.
  They read like settings; there is no config binding for any of them.

  STORAGE - two different orderings live in this class, and mixing them up has
  already caused one defect:

    channelUtilization[], utilizationTX[]
        MODULAR RINGS indexed by absolute uptime phase: (secs / p) % N. The
        index is NOT an age. The oldest bucket is (current + 1) % N, wherever
        that falls. Crossing into a bucket zeroes it.

    airtimes.period{TX,RX,RX_ALL}[]
        SHIFT-ORDERED, slot 0 newest, index IS age in hours. Rotating shifts
        everything down and clears slot 0. Slot 0 is a PARTIAL hour: a consumer
        must normalise it by getSecondsSinceBoot() % getSecondsPerPeriod(),
        which is why the HTTP report publishes both.

  MEASUREMENT WINDOW - "% of wall time", not "% of time we were awake". A
  light-sleeping node still hears traffic (the LoRa IRQ is a wake source), so
  what it misses is sub-threshold noise rather than packets. Reporting over
  observed time instead would make two nodes' readings incomparable, and this
  value is broadcast and displayed side by side.

  Two things to know before trusting the number:

    - channelUtilization measures 60s but is broadcast to the mesh at >= 1h
      cadence, so what other nodes see is a SNAPSHOT, not an average. At
      LONG_FAST and 1% real occupancy it reads exactly 0 in about 44% of
      reports.
    - the contention window it feeds moves in 20-percentage-point steps
      (map(chanutil, 0, 100, CWmin=3, CWmax=8)), so small errors in this figure
      never change the backoff.

  Rotation happens on ACCESS, not on the scheduler tick: every public method
  calls syncNow() first, and runOnce() only guarantees it happens at least once
  a second. That is deliberate - a scheduler-driven window stops advancing
  during light sleep, which is what made this over-report by up to 25x.
  test_channel_utilization_is_independent_of_scheduler_rate is what enforces it.

  TODO: airtime accuracy - the windows have known, measured defects that are
  characterised by tests in test/test_airtime but not yet fixed: the quantised
  denominator, its sawtooth, whole-packet attribution to the completing bucket,
  and getSilentMinutes() walking a modular ring as if the index were an age.
  See .notes/2026-08-05-airtime-lockguard/plan4.md phases 4b, 5, 6 and 7. Each
  fix flips a test tagged "CHARACTERISATION ->" with its phase.
*/

#define CHANNEL_UTILIZATION_PERIODS 6
#define SECONDS_PER_PERIOD 3600
#define PERIODS_TO_LOG 8
#define MINUTES_IN_HOUR 60
#define SECONDS_IN_MINUTE 60
#define MS_IN_MINUTE (SECONDS_IN_MINUTE * 1000)
#define MS_IN_HOUR (MINUTES_IN_HOUR * SECONDS_IN_MINUTE * 1000)

enum reportTypes { TX_LOG, RX_LOG, RX_ALL_LOG };

// Serialised behind `lock`. Two mechanisms, solving different halves:
//
//   - a lock-free inner core (Windows) holds ALL state and ALL logic. It has no lock and no way to
//     reach one, so nesting is impossible by construction rather than by discipline. That matters
//     here because concurrency::Lock is a non-recursive binary semaphore taken with portMAX_DELAY:
//     a nested take blocks forever and watchdog-reboots.
//   - a private Held token takes the lock in its own constructor and is the only thing that can be
//     passed where a core method demands one, so the lock cannot be forgotten either.
//
// The rule is uniform, with no exceptions to remember: every public method takes the lock exactly
// once and delegates; nothing inside ever locks. In particular isTxAllowed*() now lock like
// everything else, because they call the core rather than the public accessors.
//
// Adding state later does not change any of this - it is more state inside the core. The one rule a
// future change must keep: a new write-path helper belongs to Windows or is a free function, never a
// method on AirTime. An AirTime method would take the lock, and it would be called from inside
// logAirtime() which already holds it.
class AirTime : private concurrency::OSThread
{

  public:
    AirTime();

    void logAirtime(reportTypes reportType, uint32_t airtime_ms);
    float channelUtilizationPercent();
    float utilizationTXPercent();

    /// Compatibility shim: no caller in the tree. Kept because out-of-tree callers are plausible.
    void airtimeRotatePeriod();
    uint8_t getPeriodsToLog();
    uint32_t getSecondsPerPeriod();
    uint32_t getSecondsSinceBoot();
    /// Copies `count` buckets into `out`, newest first. Copies rather than returning the array so a
    /// caller cannot hold a handle to buckets that every other entry point rotates underneath it.
    /// False if `out` is null, `count` exceeds the log depth, or the report type is unknown.
    bool airtimeReport(reportTypes reportType, uint32_t *out, size_t count);
    uint8_t getSilentMinutes(float txPercent, float dutyCycle);
    bool isTxAllowedChannelUtil(bool polite = false);
    bool isTxAllowedAirUtil();

  private:
    concurrency::Lock lock;

#ifdef PIO_UNIT_TESTING
    // Set for the lifetime of a Held, and checked BEFORE the lock is taken so a nested take is
    // reported rather than hung at. On Portduino this is the ONLY nesting check that can work at
    // all: Lock::lock() is an empty body there, so a second take succeeds silently.
    //
    // Test builds only, and deliberately. Nothing in this tree defines DEBUG or NDEBUG, so an
    // assert() guarded on either ships to every board - which it did, until
    // nrf52_promicro_diy_tcxo (~128 bytes of headroom under its 0xEA000 warm-store cap) refused to
    // link. It would have worked on hardware, since the check precedes the guard member; the
    // objection is that abort()ing a live mesh node is a poor trade for a bug never seen in the
    // field, and that this board has no flash to spend on it.
    bool reentryFlag = false;
#endif

    /// Takes `lock` for its lifetime and doubles as proof that it is held. Only AirTime can
    /// construct one, so a core method taking `const Held &` cannot be called without the lock.
    /// A bare LockGuard would not do: it proves only that *some* lock is held.
    class Held
    {
      public:
        explicit Held(AirTime *a) : owner(armReentryCheck(a)), guard(&a->lock) {}
        ~Held();
        Held(const Held &) = delete;
        Held &operator=(const Held &) = delete;

      private:
        static AirTime *armReentryCheck(AirTime *a);
        AirTime *owner; // declared first, so its initialiser runs before the lock is taken
        concurrency::LockGuard guard;
    };

    /// All state, all logic, no lock. Cannot take one, so cannot nest.
    struct Windows {
        bool firstTime = true;
        // Time::getUptimeSecs() as of the last syncNow(); the gap since is what the windows rotate
        // by, so they stay correct even if the scheduler was paused by light sleep.
        uint32_t secSinceBoot = 0;

        // Modular rings: index is (uptime secs / period) % N, i.e. absolute phase and NOT age.
        // The oldest bucket is (current + 1) % N.
        uint32_t channelUtilization[CHANNEL_UTILIZATION_PERIODS] = {0}; // 6 x 10s
        uint32_t utilizationTX[MINUTES_IN_HOUR] = {0};                  // 60 x 60s, our TX only

        // Shift-ordered, unlike the two rings above: slot 0 is the newest hour and the index IS an
        // age. Slot 0 is a partial hour - see the storage note at the top of this file.
        struct airtimeStruct {
            uint32_t periodTX[PERIODS_TO_LOG] = {0};     // AirTime transmitted
            uint32_t periodRX[PERIODS_TO_LOG] = {0};     // AirTime received and repeated (valid mesh packets)
            uint32_t periodRX_ALL[PERIODS_TO_LOG] = {0}; // AirTime received regardless of validity. May be noise.
        } airtimes;

        void logAirtime(reportTypes reportType, uint32_t airtime_ms, const Held &);
        float channelUtilizationPercent(const Held &);
        float utilizationTXPercent(const Held &);
        bool airtimeReport(reportTypes reportType, uint32_t *out, size_t count, const Held &);
        uint8_t getSilentMinutes(float txPercent, float dutyCycle, const Held &);
        uint8_t getPeriodUtilMinute(const Held &);
        uint8_t getPeriodUtilHour(const Held &);
        // Advance rolling airtime windows from monotonic uptime, not from runOnce() calls.
        void syncNow(const Held &);
    } w;

    uint8_t max_channel_util_percent = 40;
    uint8_t polite_channel_util_percent = 25;
    uint8_t polite_duty_cycle_percent = 50; // half of Duty Cycle allowance is ok for metadata

  protected:
    virtual int32_t runOnce() override;
};

extern AirTime *airTime;
