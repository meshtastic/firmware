#include "TestUtil.h"
#include "gps/RTC.h"
#include <time.h>
#include <unity.h>

static const time_t kUptimeTime = 21;
static const uint32_t kAllowedDriftSeconds = 1;

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

static void test_readFromRTC_preserves_better_network_time(void)
{
    resetRTCStateForTests();

    const time_t networkEpoch = makeValidEpoch();
    struct timeval networkTime;
    networkTime.tv_sec = networkEpoch;
    networkTime.tv_usec = 0;

    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, perhapsSetRTC(RTCQualityFromNet, &networkTime));

    struct timeval uptimeTime;
    uptimeTime.tv_sec = kUptimeTime;
    uptimeTime.tv_usec = 0;
    setRTCSystemTimeForTests(&uptimeTime);
    setReadFromRTCUseSystemTimeForTests(true);

    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, readFromRTC());
    TEST_ASSERT_EQUAL_INT(RTCQualityFromNet, getRTCQuality());
    TEST_ASSERT_UINT32_WITHIN(kAllowedDriftSeconds, networkEpoch, getValidTime(RTCQualityFromNet));
}

static void test_readFromRTC_initializes_time_before_any_better_source(void)
{
    resetRTCStateForTests();

    const time_t systemEpoch = makeValidEpoch();
    struct timeval systemTime;
    systemTime.tv_sec = systemEpoch;
    systemTime.tv_usec = 0;
    setRTCSystemTimeForTests(&systemTime);
    setReadFromRTCUseSystemTimeForTests(true);

    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, readFromRTC());
    TEST_ASSERT_EQUAL_INT(RTCQualityNone, getRTCQuality());
    TEST_ASSERT_UINT32_WITHIN(kAllowedDriftSeconds, systemEpoch, getTime());
}

void setup()
{
    delay(10);
    initializeTestEnvironment();

    UNITY_BEGIN();
    RUN_TEST(test_readFromRTC_preserves_better_network_time);
    RUN_TEST(test_readFromRTC_initializes_time_before_any_better_source);
    exit(UNITY_END());
}

void loop() {}
