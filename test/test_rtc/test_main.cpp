#include "TestUtil.h"
#include "gps/RTC.h"
#include <sys/time.h>
#include <time.h>
#include <unity.h>

// Regression coverage for issue #9828: on boards without a hardware RTC (e.g. RP2040),
// gettimeofday() can return uptime seconds rather than wall-clock time. A later readFromRTC()
// must not overwrite a higher-quality network/GPS time with that value, but it should still seed
// the clock when nothing better exists yet.
//
// The native test build compiles the RV3028 hardware-RTC branch (variants/native/portduino
// defines RV3028_RTC), so these tests use setReadFromRTCUseSystemTimeForTests() to force the
// no-hardware-RTC fallback path and setRTCSystemTimeForTests() to inject a deterministic clock.

static const uint32_t kAllowedDriftSeconds = 2;
static const time_t kUptimeSeconds = 21; // what gettimeofday() returns on RP2040 without a real clock

// A clearly-valid wall-clock epoch, safely inside any BUILD_EPOCH validity window.
static time_t makeValidEpoch()
{
    return time(NULL) + SEC_PER_DAY;
}

void setUp(void)
{
    resetRTCStateForTests();
}

void tearDown(void)
{
    resetRTCStateForTests();
}

// A higher-quality network time must survive a later system-time read that only knows uptime.
static void test_readFromRTC_preserves_better_network_time(void)
{
    const time_t networkEpoch = makeValidEpoch();
    struct timeval networkTime;
    networkTime.tv_sec = networkEpoch;
    networkTime.tv_usec = 0;
    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, perhapsSetRTC(RTCQualityFromNet, &networkTime));

    // Simulate a later readFromRTC() falling back to a system clock that only knows uptime.
    struct timeval uptime;
    uptime.tv_sec = kUptimeSeconds;
    uptime.tv_usec = 0;
    setRTCSystemTimeForTests(&uptime);
    setReadFromRTCUseSystemTimeForTests(true);

    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, readFromRTC());
    TEST_ASSERT_EQUAL_INT(RTCQualityFromNet, getRTCQuality());
    TEST_ASSERT_UINT32_WITHIN(kAllowedDriftSeconds, (uint32_t)networkEpoch, getValidTime(RTCQualityFromNet));
}

// Before any higher-quality source exists, the fallback should still seed the clock.
static void test_readFromRTC_initializes_time_when_no_better_source(void)
{
    const time_t systemEpoch = makeValidEpoch();
    struct timeval systemTime;
    systemTime.tv_sec = systemEpoch;
    systemTime.tv_usec = 0;
    setRTCSystemTimeForTests(&systemTime);
    setReadFromRTCUseSystemTimeForTests(true);

    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, readFromRTC());
    TEST_ASSERT_EQUAL_INT(RTCQualityNone, getRTCQuality());
    TEST_ASSERT_UINT32_WITHIN(kAllowedDriftSeconds, (uint32_t)systemEpoch, getTime());
}

void setup()
{
    delay(10);
    initializeTestEnvironment();

    UNITY_BEGIN();
    RUN_TEST(test_readFromRTC_preserves_better_network_time);
    RUN_TEST(test_readFromRTC_initializes_time_when_no_better_source);
    exit(UNITY_END());
}

void loop() {}
