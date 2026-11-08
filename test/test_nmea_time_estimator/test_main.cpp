// Unit tests for nmeaSentenceArrival(), NmeaTimeEstimator and shouldApplyNmeaTime() in
// src/gps/NmeaTimeEstimator.cpp - how GPS::whileActive() turns NMEA bytes read in 200 ms batches into UTC.
//
// The GPS thread drains the UART every ~200 ms, so the moment TinyGPS++ parses an RMC sentence says little about
// when the sentence arrived. Each drain instead bounds it: every byte read after the sentence also had to cross the
// line before the drain ended (upper bound), and nothing read in the drain was readable when the previous drain saw
// the buffer empty (lower bound). The estimator intersects those bounds over many sentences and reports the upper
// one, so the clock it sets can lag the module's report but never runs ahead of it.
//
// Regressions guarded:
//  - an off-by-one in the byte arithmetic, or a sign flip, which would put the clock ahead of the GPS;
//  - the window not restarting on an inconsistent sample, which would pin the clock to a stale offset after the
//    module's time steps (leap second, re-acquisition);
//  - the apply policy stepping the clock back on every noisy estimate, or never stepping it back, which lets a
//    fast crystal on an always-on GPS run ahead for the rest of the boot.
#include "Arduino.h" // declares setup()/loop() with the linkage the framework's main() calls
#include "TestUtil.h"
#include "gps/NmeaTimeEstimator.h"
#include <cstdlib>
#include <stdint.h>
#include <unity.h>

static constexpr uint32_t kByteUs9600 = 1042; // one 8N1 byte at 9600 baud, as gpsByteTimeUs() rounds it

void setUp(void) {}
void tearDown(void) {}

// --- nmeaSentenceArrival ---

// The '$' sits (length - 1) bytes before the terminator, and the bytes read after the terminator each took a byte time.
static void test_arrival_upper_bound_backdates_by_later_bytes_and_sentence_length(void)
{
    const NmeaDrain drain = {800, 1000, 100, kByteUs9600, 5000};
    const NmeaArrivalBounds b = nmeaSentenceArrival(drain, 59, 60);
    // endMs is a floor, so the bound starts 1 ms after it.
    TEST_ASSERT_EQUAL_INT64(1000 * 1000 + 1000 - 40 * (int64_t)kByteUs9600 - 59 * (int64_t)kByteUs9600, b.hiUs);
}

// The first byte of the drain arrived no earlier than the previous drain's end minus the UART hold-back; the
// terminator came lastIndex bytes later and the '$' (length - 1) bytes before that.
static void test_arrival_lower_bound_from_previous_drain(void)
{
    const NmeaDrain drain = {800, 1000, 100, kByteUs9600, 5000};
    const NmeaArrivalBounds b = nmeaSentenceArrival(drain, 59, 60);
    TEST_ASSERT_TRUE(b.hasLo);
    TEST_ASSERT_EQUAL_INT64(800 * 1000 - 5000, b.loUs);
    TEST_ASSERT_TRUE(b.loUs <= b.hiUs);
}

// gpsd sockets and bridged UARTs deliver in bulk, so byte counts prove nothing: only "it was here by endMs" holds.
static void test_arrival_without_line_rate_has_only_an_upper_bound(void)
{
    const NmeaDrain drain = {800, 1000, 100, 0, 5000};
    const NmeaArrivalBounds b = nmeaSentenceArrival(drain, 59, 60);
    TEST_ASSERT_FALSE(b.hasLo);
    TEST_ASSERT_EQUAL_INT64(1001 * 1000, b.hiUs);
}

// The first drain after boot has no previous empty buffer to measure from.
static void test_arrival_without_previous_drain_has_no_lower_bound(void)
{
    const NmeaDrain drain = {0, 1000, 100, kByteUs9600, 5000};
    TEST_ASSERT_FALSE(nmeaSentenceArrival(drain, 59, 60).hasLo);
}

// --- NmeaTimeEstimator: a simulated receiver ---

// A module that starts each 1 Hz burst kLatencyUs after the UTC second, RMC first, at 9600 baud, read by a thread
// that drains every 200 ms plus a few ms of scheduling jitter - the shape GPS::runOnce() produces.
namespace
{
constexpr int64_t kEpoch0 = 1800000000;   // UTC seconds of the first burst
constexpr int64_t kMono0Us = 5 * 1000000; // monotonic µs at that UTC second
constexpr int64_t kLatencyUs = 73000;     // second boundary to the first byte of the burst
constexpr uint32_t kRmcLen = 70;          // '$' .. '\r'
constexpr uint32_t kBurstLen = 420;       // whole burst, RMC included
constexpr int64_t kByteUs = kByteUs9600;

// Monotonic µs at which byte k (0-based) of the burst for UTC second s has fully arrived.
int64_t byteArrivalUs(int64_t s, uint32_t k)
{
    return kMono0Us + (s - kEpoch0) * 1000000 + kLatencyUs + (int64_t)(k + 1) * kByteUs;
}

// The offset the estimator is bounding: monotonic µs at the RMC '$' minus the UTC µs the RMC names.
int64_t trueOffsetUs()
{
    return byteArrivalUs(kEpoch0, 0) - kEpoch0 * 1000000;
}

struct SimReceiver {
    int64_t nextSecond = kEpoch0;
    uint32_t nextByte = 0;
    uint64_t prevEndMs = 0;
    uint32_t jitterSeed = 12345;

    uint32_t jitterMs()
    {
        jitterSeed = jitterSeed * 1103515245 + 12345;
        return (jitterSeed >> 16) % 8;
    }

    // Read every byte that has arrived by nowUs, as one drain, feeding any completed RMC to the estimator.
    void drain(int64_t nowUs, NmeaTimeEstimator &est)
    {
        uint32_t bytes = 0;
        bool haveRmc = false;
        uint32_t rmcLastIndex = 0;
        int64_t rmcSecond = 0;
        while (byteArrivalUs(nextSecond, nextByte) <= nowUs) {
            if (nextByte == kRmcLen - 1) {
                haveRmc = true;
                rmcLastIndex = bytes;
                rmcSecond = nextSecond;
            }
            bytes++;
            if (++nextByte == kBurstLen) {
                nextByte = 0;
                nextSecond++;
            }
        }
        const uint64_t endMs = (uint64_t)(nowUs / 1000);
        const NmeaDrain d = {prevEndMs, endMs, bytes, (uint32_t)kByteUs, 3 * (uint32_t)kByteUs + 2000};
        if (haveRmc)
            est.addSample(rmcSecond * 1000, nmeaSentenceArrival(d, rmcLastIndex, kRmcLen));
        prevEndMs = endMs;
    }
};
} // namespace

// Over ~30 s of bursts the estimate must converge to within a few ms of the module's report and, at every step,
// never put UTC ahead of it.
static void test_estimator_converges_and_never_runs_ahead(void)
{
    NmeaTimeEstimator est;
    SimReceiver rx;
    int64_t nowUs = kMono0Us - 150000;
    for (int i = 0; i < 150; i++) {
        nowUs += (200 + rx.jitterMs()) * 1000;
        rx.drain(nowUs, est);
        if (!est.hasEstimate())
            continue;
        const uint64_t monoMs = (uint64_t)(nowUs / 1000);
        const int64_t trueUtcMs = ((int64_t)monoMs * 1000 - trueOffsetUs()) / 1000;
        TEST_ASSERT_TRUE((int64_t)est.utcMillisAt(monoMs) <= trueUtcMs);
    }
    TEST_ASSERT_TRUE(est.sampleCount() >= 25);
    const uint64_t monoMs = (uint64_t)(nowUs / 1000);
    const int64_t trueUtcMs = ((int64_t)monoMs * 1000 - trueOffsetUs()) / 1000;
    TEST_ASSERT_INT64_WITHIN(3, trueUtcMs, (int64_t)est.utcMillisAt(monoMs));
    // Bounded on both sides, and the reported width covers the remaining lag.
    TEST_ASSERT_TRUE(est.uncertaintyUs() < UINT32_MAX);
    TEST_ASSERT_TRUE((int64_t)est.uncertaintyUs() >= (trueUtcMs - (int64_t)est.utcMillisAt(monoMs)) * 1000 - 1000);
}

// A sample that cannot overlap the window (the module's time stepped by a second) restarts it on that sample.
static void test_disagreeing_sample_restarts_window(void)
{
    NmeaTimeEstimator est;
    NmeaArrivalBounds a;
    a.hasLo = true;
    a.loUs = 1000000;
    a.hiUs = 1010000;
    est.addSample(kEpoch0 * 1000, a);
    a.loUs += 1000000;
    a.hiUs += 1000000;
    est.addSample((kEpoch0 + 1) * 1000, a);
    TEST_ASSERT_EQUAL_UINT32(2, est.sampleCount());

    a.loUs += 1000000;
    a.hiUs += 1000000;
    est.addSample((kEpoch0 + 3) * 1000, a); // one second off
    TEST_ASSERT_EQUAL_UINT32(1, est.sampleCount());
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(kEpoch0 + 3) * 1000, est.utcMillisAt(a.hiUs / 1000));
}

// Clocks drift apart, so a window older than MAX_WINDOW_MS starts over rather than intersecting stale bounds.
static void test_old_window_restarts(void)
{
    NmeaTimeEstimator est;
    NmeaArrivalBounds a;
    a.hiUs = 1000000;
    est.addSample(kEpoch0 * 1000, a);
    a.hiUs += (int64_t)NmeaTimeEstimator::MAX_WINDOW_MS * 1000;
    est.addSample((kEpoch0 + NmeaTimeEstimator::MAX_WINDOW_MS / 1000) * 1000, a);
    TEST_ASSERT_EQUAL_UINT32(2, est.sampleCount());
    a.hiUs += 1000000;
    est.addSample((kEpoch0 + NmeaTimeEstimator::MAX_WINDOW_MS / 1000 + 1) * 1000, a);
    TEST_ASSERT_EQUAL_UINT32(1, est.sampleCount());
}

// With only upper bounds the estimate is still usable (gpsd), but the width is reported as unknown.
static void test_upper_bounds_only(void)
{
    NmeaTimeEstimator est;
    TEST_ASSERT_FALSE(est.hasEstimate());
    NmeaArrivalBounds a;
    a.hiUs = 2000000;
    est.addSample(kEpoch0 * 1000 + 500, a);
    TEST_ASSERT_TRUE(est.hasEstimate());
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, est.uncertaintyUs());
    TEST_ASSERT_EQUAL_UINT64((uint64_t)kEpoch0 * 1000 + 500 + 250, est.utcMillisAt(2250));
}

// --- shouldApplyNmeaTime ---

static void test_apply_when_clock_is_not_gps(void)
{
    TEST_ASSERT_TRUE(shouldApplyNmeaTime(false, 0, 0, 1, false));
}

// The estimate is a lower bound on UTC, so a clock behind it is wrong; a 1 ms lag is the millisecond clock's floor.
static void test_apply_forward_steps_only_past_threshold(void)
{
    TEST_ASSERT_TRUE(shouldApplyNmeaTime(true, -2, 0, 1, false));
    TEST_ASSERT_FALSE(shouldApplyNmeaTime(true, -1, 0, 1, false));
}

// A clock ahead of the estimate may well be right, so stepping back waits for the drift resync.
static void test_apply_back_steps_only_on_resync(void)
{
    TEST_ASSERT_FALSE(shouldApplyNmeaTime(true, 50, 9 * 60 * 1000, 30, false));
    TEST_ASSERT_TRUE(shouldApplyNmeaTime(true, 50, 10 * 60 * 1000, 5, false));
    TEST_ASSERT_FALSE(shouldApplyNmeaTime(true, 50, 10 * 60 * 1000, 4, false));
    TEST_ASSERT_TRUE(shouldApplyNmeaTime(true, 50, UINT64_MAX, 5, false));
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_arrival_upper_bound_backdates_by_later_bytes_and_sentence_length);
    RUN_TEST(test_arrival_lower_bound_from_previous_drain);
    RUN_TEST(test_arrival_without_line_rate_has_only_an_upper_bound);
    RUN_TEST(test_arrival_without_previous_drain_has_no_lower_bound);
    RUN_TEST(test_estimator_converges_and_never_runs_ahead);
    RUN_TEST(test_disagreeing_sample_restarts_window);
    RUN_TEST(test_old_window_restarts);
    RUN_TEST(test_upper_bounds_only);
    RUN_TEST(test_apply_when_clock_is_not_gps);
    RUN_TEST(test_apply_forward_steps_only_past_threshold);
    RUN_TEST(test_apply_back_steps_only_on_resync);
    exit(UNITY_END());
}

void loop() {}
