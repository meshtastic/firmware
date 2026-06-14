// Unit tests for src/Time.{h,cpp} — the monotonic uptime seam.
// Covers: test-clock injection, getMillis64() low-word fidelity, rollover across 0xFFFFFFFF,
// and that ordinary forward steps do not register a spurious wrap.
//
// getMillis64() keeps persistent static carry state across calls, so tests assert on the
// *delta* between two consecutive samples rather than absolute high-word values — that makes
// each test independent of suite ordering.
#include "Arduino.h"
#include "TestUtil.h"
#include "Time.h"
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

// --- getMillis64 low word ---

void test_getMillis64_low_word_matches_getMillis()
{
    Time::setTestMillis(0x0BADF00D);
    uint64_t v = Time::getMillis64();
    TEST_ASSERT_EQUAL_UINT32(0x0BADF00D, static_cast<uint32_t>(v & 0xFFFFFFFFu));
}

// --- rollover immunity (the headline behaviour) ---

void test_getMillis64_increments_high_word_on_wrap()
{
    Time::setTestMillis(0xFFFFFF00u);
    uint64_t a = Time::getMillis64();
    Time::setTestMillis(0x00000100u); // low word wrapped past 0xFFFFFFFF
    uint64_t b = Time::getMillis64();

    // High word advanced by exactly one; low word reflects the new value.
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(a >> 32) + 1, static_cast<uint32_t>(b >> 32));
    TEST_ASSERT_EQUAL_UINT32(0x00000100u, static_cast<uint32_t>(b & 0xFFFFFFFFu));
    // And b is strictly greater than a despite the 32-bit value going "backwards".
    TEST_ASSERT_TRUE(b > a);
}

void test_getMillis64_no_spurious_wrap_on_forward_step()
{
    Time::setTestMillis(0x00001000u);
    uint64_t a = Time::getMillis64();
    Time::setTestMillis(0x00002000u);
    uint64_t b = Time::getMillis64();

    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(a >> 32), static_cast<uint32_t>(b >> 32)); // no wrap
    TEST_ASSERT_EQUAL_UINT64((uint64_t)0x1000u, b - a);                                       // exact delta
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
    RUN_TEST(test_getMillis64_low_word_matches_getMillis);
    RUN_TEST(test_getMillis64_increments_high_word_on_wrap);
    RUN_TEST(test_getMillis64_no_spurious_wrap_on_forward_step);
    RUN_TEST(test_real_clock_advances_when_not_injected);
    exit(UNITY_END());
}

void loop() {}
