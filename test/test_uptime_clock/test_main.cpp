// Unit tests for src/UptimeClock.{h,cpp} - the monotonic uptime seam.
// Covers: test-clock injection, stepping the injected clock, and the real-clock fallback.
//
// Wrap behaviour is not tested here because getMillis() is a plain 32-bit read with no wrap
// handling of its own - the wrap is handled by the consumers, and is tested in test_throttle/.
#include "Arduino.h"
#include "TestUtil.h"
#include "UptimeClock.h"
#include <cstdint>
#include <unity.h>

void setUp(void) {}
void tearDown(void)
{
    Time::useRealClock(); // don't leak the fake clock into other suites
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
    RUN_TEST(test_real_clock_advances_when_not_injected);
    exit(UNITY_END());
}

void loop() {}
