// Unit tests for src/UptimeClock.{h,cpp} - the monotonic uptime seam.
// Covers: test-clock injection, stepping the injected clock, the real-clock fallback, and the
// single-writer wrap carry (readers derive, serviceMonotonic() publishes). getMillis() itself is a
// plain 32-bit read with no wrap handling of its own - its consumers' wrap arithmetic is tested in
// test_throttle/.
#include "Arduino.h"
#include "TestUtil.h"
#include "UptimeClock.h"
#include "gps/RTC.h"
#include <atomic>
#include <cstdint>
#include <sys/time.h>
#include <thread>
#include <unity.h>
#include <vector>

void setUp(void)
{
    Time::resetMonotonicForTests(); // absolute uptime assertions must not depend on case order
}
void tearDown(void)
{
    Time::useRealClock(); // don't leak the fake clock into other suites
    resetRTCStateForTests();
}

// Step the injected clock the way the firmware does: the main loop calls serviceMonotonic() every
// iteration, so any advance is followed by a publish.
static void advanceAndService(uint32_t deltaMs)
{
    Time::advanceTestMillis(deltaMs);
    Time::serviceMonotonic();
}

// --- injection ---

void test_getMillis_returns_injected_value()
{
    Time::setTestMillis(123456);
    TEST_ASSERT_EQUAL_UINT32(123456, Time::getMillis());
}

void test_advanceTestMillis_steps_clock()
{
    Time::setTestMillis(1000);
    Time::advanceTestMillis(500);
    TEST_ASSERT_EQUAL_UINT32(1500, Time::getMillis());
}

// Advancing past 0xFFFFFFFF wraps like millis() does, rather than saturating. This is the property
// the Throttle wrap tests are built on, so it is worth pinning here too.
void test_advanceTestMillis_wraps_like_millis()
{
    Time::setTestMillis(0xFFFFFF00u);
    Time::advanceTestMillis(0x200u);
    TEST_ASSERT_EQUAL_UINT32(0x00000100u, Time::getMillis());
}

// --- getMillisMonotonic(): the published wrap carry ---

void test_monotonic_matches_millis_before_any_wrap()
{
    Time::setTestMillis(123456);
    TEST_ASSERT_EQUAL_UINT64(123456u, Time::getMillisMonotonic());
}

void test_monotonic_counts_a_wrap()
{
    Time::setTestMillis(0xFFFFFF00u);
    Time::serviceMonotonic();
    TEST_ASSERT_EQUAL_UINT64(0xFFFFFF00u, Time::getMillisMonotonic());

    advanceAndService(0x200u); // crosses the 32-bit wrap; low word is now 0x00000100
    TEST_ASSERT_EQUAL_UINT64(0x100000100ull, Time::getMillisMonotonic());
}

// The property that lets readers stay pure: a reader adds its own unsigned elapsed time to the
// published snapshot, so it is exact across a wrap that no publish has observed yet. Nothing here
// needs to detect the boundary, which is why concurrent readers cannot double-count it.
void test_monotonic_reader_crosses_the_wrap_without_a_publish()
{
    Time::setTestMillis(0xFFFFFF00u);
    Time::serviceMonotonic(); // last publish before the wrap

    Time::advanceTestMillis(0x200u); // cross the wrap with no publish at all
    TEST_ASSERT_EQUAL_UINT64(0x100000100ull, Time::getMillisMonotonic());
}

// Reads must not advance the carry. Under the old read-modify-write accessor each reader bumped
// the wrap counter itself, which is what made two of them able to count one wrap twice.
void test_monotonic_reads_do_not_advance_the_carry()
{
    Time::setTestMillis(0xFFFFFF00u);
    Time::serviceMonotonic();

    Time::advanceTestMillis(0x200u);
    for (int i = 0; i < 8; i++)
        TEST_ASSERT_EQUAL_UINT64(0x100000100ull, Time::getMillisMonotonic());

    Time::serviceMonotonic(); // the eight reads must not have left eight wraps behind
    TEST_ASSERT_EQUAL_UINT64(0x100000100ull, Time::getMillisMonotonic());
}

void test_monotonic_counts_every_wrap_when_serviced_each_window()
{
    Time::setTestMillis(0x80000000u);
    Time::serviceMonotonic();
    TEST_ASSERT_EQUAL_UINT64(0x80000000ull, Time::getMillisMonotonic());

    // Three full 2^32 cycles, published once per half-cycle - well inside the required
    // one-publish-per-49.7-days window.
    for (int wrap = 1; wrap <= 3; wrap++) {
        advanceAndService(0x80000000u); // crosses the wrap; low word back to 0
        advanceAndService(0x80000000u); // completes the cycle; low word back to 0x80000000
        TEST_ASSERT_EQUAL_UINT64(0x80000000ull + ((uint64_t)wrap << 32), Time::getMillisMonotonic());
    }
}

// The documented contract, pinned: a full 2^32 ms elapsing between two publishes is
// indistinguishable from no time passing, so the wrap is lost. This is why the main loop's
// per-iteration serviceMonotonic() matters - and it is now the only obligation, where before every
// reader had to participate.
void test_monotonic_misses_a_wrap_not_serviced_within_the_window()
{
    Time::setTestMillis(1000);
    Time::serviceMonotonic();
    TEST_ASSERT_EQUAL_UINT64(1000u, Time::getMillisMonotonic());

    Time::advanceTestMillis(0x80000000u);
    advanceAndService(0x80000000u); // full cycle with no publish in between: low word is 1000 again

    TEST_ASSERT_EQUAL_UINT64(1000u, Time::getMillisMonotonic()); // the elapsed 2^32 ms is lost
}

void test_getUptimeSecs_stays_exact_across_the_wrap()
{
    Time::setTestMillis(4294967000u); // 4294967 whole seconds, 296ms short of the wrap
    Time::serviceMonotonic();
    TEST_ASSERT_EQUAL_UINT32(4294967u, Time::getUptimeSecs());

    advanceAndService(1000); // crosses the wrap
    TEST_ASSERT_EQUAL_UINT32(4294968u, Time::getUptimeSecs());
}

// --- concurrent readers ---

// Readers run flat out while the clock is stepped across several wraps. Under the old accessor two
// readers interleaving inside the wrap window could each bump the counter, jumping every later
// reading 2^32 ms forward; here they only ever read, so the final value has to be exact.
//
// A one-instruction race is not something a test can hit on demand, so this is corroboration
// rather than the guarantee - the guarantee is structural, and test_monotonic_reads_do_not_advance
// _the_carry pins it. What this case does catch is any future change that puts a write back on the
// read path.
void test_monotonic_exact_with_concurrent_readers()
{
    constexpr int kReaders = 4;
    constexpr int kWraps = 3;
    constexpr uint32_t kStep = 0x40000000u; // quarter of a cycle, so each wrap is crossed mid-step

    Time::setTestMillis(0xFFFFF000u);
    Time::serviceMonotonic();

    std::atomic<bool> stop{false};
    std::atomic<bool> wentBackwards{false};
    std::vector<std::thread> readers;
    for (int i = 0; i < kReaders; i++) {
        readers.emplace_back([&stop, &wentBackwards]() {
            uint64_t previous = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                const uint64_t now = Time::getMillisMonotonic();
                if (now < previous)
                    wentBackwards.store(true, std::memory_order_relaxed);
                previous = now;
            }
        });
    }

    uint64_t expected = 0xFFFFF000ull;
    for (int i = 0; i < kWraps * 4; i++) {
        advanceAndService(kStep);
        expected += kStep;
    }

    stop.store(true, std::memory_order_relaxed);
    for (auto &reader : readers)
        reader.join();

    TEST_ASSERT_FALSE_MESSAGE(wentBackwards.load(std::memory_order_relaxed), "monotonic clock retreated for a reader");
    TEST_ASSERT_EQUAL_UINT64(expected, Time::getMillisMonotonic());
}

// --- getTime(): the wall clock must not retreat at the millis() wrap ---

// Epoch used by the wall-clock cases; must sit between BUILD_EPOCH (stamped at build time) and
// BUILD_EPOCH + 40 years or perhapsSetRTC() rejects it as implausible - so derive it.
#ifdef BUILD_EPOCH
static constexpr uint32_t kTestEpoch = (uint32_t)BUILD_EPOCH + 3600;
#else
static constexpr uint32_t kTestEpoch = 1800000000u;
#endif

void test_getTime_stays_exact_across_the_wrap()
{
    resetRTCStateForTests();
    Time::setTestMillis(0xFFFFFF00u); // 256ms short of the wrap
    Time::serviceMonotonic();

    struct timeval tv = {};
    tv.tv_sec = kTestEpoch;
    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, perhapsSetRTC(RTCQualityFromNet, &tv));
    TEST_ASSERT_EQUAL_UINT32(kTestEpoch, getTime(false));

    advanceAndService(400u * 1000u); // crosses the wrap partway through
    // With a 32-bit anchor this read came back 49.7 days in the past.
    TEST_ASSERT_EQUAL_UINT32(kTestEpoch + 400, getTime(false));
}

// The anchor must also be correct when the time-set itself happens after a counted wrap, i.e.
// when the monotonic clock is already past 32-bit range.
void test_getTime_anchored_after_a_wrap_is_exact()
{
    resetRTCStateForTests();
    Time::setTestMillis(0xFFFFFF00u);
    Time::serviceMonotonic();  // latch the pre-wrap value
    advanceAndService(0x200u); // cross the wrap; monotonic is now > 2^32

    struct timeval tv = {};
    tv.tv_sec = kTestEpoch;
    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, perhapsSetRTC(RTCQualityFromNet, &tv));

    advanceAndService(100u * 1000u);
    TEST_ASSERT_EQUAL_UINT32(kTestEpoch + 100, getTime(false));
}

// A reader on another thread must not be able to perturb the wall clock. This is the user-visible
// shape of the race: getTime() is reached from the nRF52 BLE task and the portduino web server
// threads, and a double-counted wrap put every rx_time and last_heard ~49.7 days in the future.
void test_getTime_unaffected_by_concurrent_readers_across_the_wrap()
{
    resetRTCStateForTests();
    Time::setTestMillis(0xFFFFF000u);
    Time::serviceMonotonic();

    struct timeval tv = {};
    tv.tv_sec = kTestEpoch;
    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, perhapsSetRTC(RTCQualityFromNet, &tv));

    std::atomic<bool> stop{false};
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; i++) {
        readers.emplace_back([&stop]() {
            while (!stop.load(std::memory_order_relaxed))
                (void)getTime(false); // what the BLE / web-server threads actually call
        });
    }

    advanceAndService(0x800u);      // cross the wrap while the readers are running
    advanceAndService(60u * 1000u); // and some ordinary time after it

    stop.store(true, std::memory_order_relaxed);
    for (auto &reader : readers)
        reader.join();

    TEST_ASSERT_EQUAL_UINT32(kTestEpoch + 62, getTime(false)); // 0x800ms + 60s, rounded down
}

// --- real clock fallback ---

void test_real_clock_advances_when_not_injected()
{
    Time::useRealClock();
    uint32_t t0 = Time::getMillis();
    testDelay(5);
    uint32_t t1 = Time::getMillis();
    TEST_ASSERT_TRUE(t1 >= t0); // real millis() is monotonic over a short delay
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_getMillis_returns_injected_value);
    RUN_TEST(test_advanceTestMillis_steps_clock);
    RUN_TEST(test_advanceTestMillis_wraps_like_millis);
    RUN_TEST(test_monotonic_matches_millis_before_any_wrap);
    RUN_TEST(test_monotonic_counts_a_wrap);
    RUN_TEST(test_monotonic_reader_crosses_the_wrap_without_a_publish);
    RUN_TEST(test_monotonic_reads_do_not_advance_the_carry);
    RUN_TEST(test_monotonic_counts_every_wrap_when_serviced_each_window);
    RUN_TEST(test_monotonic_misses_a_wrap_not_serviced_within_the_window);
    RUN_TEST(test_getUptimeSecs_stays_exact_across_the_wrap);
    RUN_TEST(test_monotonic_exact_with_concurrent_readers);
    RUN_TEST(test_getTime_stays_exact_across_the_wrap);
    RUN_TEST(test_getTime_anchored_after_a_wrap_is_exact);
    RUN_TEST(test_getTime_unaffected_by_concurrent_readers_across_the_wrap);
    RUN_TEST(test_real_clock_advances_when_not_injected);
    exit(UNITY_END());
}

void loop() {}
