#include "TestUtil.h"
#include "UptimeClock.h"
#include "gps/RTC.h"
#include <cstdio>
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

// Mirrors FORTY_YEARS in RTC.h, which is only visible when BUILD_EPOCH is defined. BUILD_EPOCH is
// injected by bin/platformio-custom.py into the src/ build (projenv) but not into test sources, so
// this TU cannot #ifdef on it; the bounds tests below probe for it at runtime instead.
static const uint64_t kFortyYears = 40ULL * 365 * SEC_PER_DAY;

#define MSG_BUF_LEN 200
#define TEST_MSG_FMT(fmt, ...)                                                                                                   \
    do {                                                                                                                         \
        char _buf[MSG_BUF_LEN];                                                                                                  \
        snprintf(_buf, sizeof(_buf), fmt, __VA_ARGS__);                                                                          \
        TEST_MESSAGE(_buf);                                                                                                      \
    } while (0)

// A clearly-valid wall-clock epoch, safely inside any BUILD_EPOCH validity window.
static time_t makeValidEpoch()
{
    return time(NULL) + SEC_PER_DAY;
}

static struct timeval makeTv(time_t secs)
{
    struct timeval tv;
    tv.tv_sec = secs;
    tv.tv_usec = 0;
    return tv;
}

// Freeze the injected uptime clock at baseMs. perhapsSetRTC() anchors timeStartMs64 at the fake
// "now", so while the clock is frozen getTime() returns the applied epoch exactly - no drift
// tolerance needed. Reset the wrap carry first: a prior test may have published a larger instant,
// and stepping the clock backwards past a published snapshot reads as a ~49.7-day wrap.
static void beginFakeClock(uint32_t baseMs)
{
    Time::resetMonotonicForTests();
    Time::setTestMillis(baseMs);
    Time::serviceMonotonic();
}

// Step the injected clock the way the firmware does: every advance is followed by a publish.
static void advanceFakeClock(uint32_t deltaMs)
{
    Time::advanceTestMillis(deltaMs);
    Time::serviceMonotonic();
}

void setUp(void)
{
    resetRTCStateForTests();
}

void tearDown(void)
{
    Time::useRealClock(); // don't leak the fake clock into later tests or other suites
    Time::resetMonotonicForTests();
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

// --- perhapsSetRTC(timeval) quality arbitration ---

// FromNet/Device sources are always rejected below a higher quality, and the rejection must
// leave quality and the running clock untouched (the #9828 mesh-time-poisoning family). NTP
// below GPS is rejected only while the 30-min drift throttle (stamped by the GPS set) is live;
// after it expires, NTP deliberately replaces even GPS-quality time (RTC.cpp drift-correction
// branch) - both halves are pinned here.
static void test_downgrade_rejected_state_untouched(void)
{
    beginFakeClock(60 * 1000);
    const time_t gpsEpoch = makeValidEpoch();
    struct timeval tv = makeTv(gpsEpoch);
    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, perhapsSetRTC(RTCQualityGPS, &tv));
    TEST_ASSERT_EQUAL_INT(RTCQualityGPS, getRTCQuality());
    TEST_ASSERT_EQUAL_UINT32((uint32_t)gpsEpoch, getTime());

    struct timeval poison = makeTv(gpsEpoch + 777);
    TEST_ASSERT_EQUAL_INT(RTCSetResultNotSet, perhapsSetRTC(RTCQualityFromNet, &poison));
    TEST_ASSERT_EQUAL_INT(RTCSetResultNotSet, perhapsSetRTC(RTCQualityDevice, &poison));
    // NTP below GPS: within 30 minutes of the GPS set (which stamped the drift throttle), rejected.
    TEST_ASSERT_EQUAL_INT(RTCSetResultNotSet, perhapsSetRTC(RTCQualityNTP, &poison));
    TEST_ASSERT_EQUAL_INT(RTCQualityGPS, getRTCQuality());

    // Time still tracks the GPS epoch, not the rejected one.
    advanceFakeClock(5 * 1000);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)gpsEpoch + 5, getTime());

    // Once the drift throttle expires, NTP replaces GPS-quality time on purpose (drift
    // correction), while FromNet/Device stay rejected: the throttle escape is NTP-only.
    advanceFakeClock(31 * 60 * 1000);
    struct timeval stillPoison = makeTv(gpsEpoch + 555);
    TEST_ASSERT_EQUAL_INT(RTCSetResultNotSet, perhapsSetRTC(RTCQualityFromNet, &stillPoison));
    TEST_ASSERT_EQUAL_INT(RTCSetResultNotSet, perhapsSetRTC(RTCQualityDevice, &stillPoison));
    TEST_ASSERT_EQUAL_INT(RTCQualityGPS, getRTCQuality());
    struct timeval drift = makeTv(gpsEpoch + 999);
    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, perhapsSetRTC(RTCQualityNTP, &drift));
    TEST_ASSERT_EQUAL_INT(RTCQualityNTP, getRTCQuality());
    TEST_ASSERT_EQUAL_UINT32((uint32_t)gpsEpoch + 999, getTime());
}

// Equal-quality FromNet has no reapply branch: the second set is ignored.
static void test_equal_quality_fromnet_is_not_reapplied(void)
{
    beginFakeClock(60 * 1000);
    const time_t firstEpoch = makeValidEpoch();
    struct timeval tv = makeTv(firstEpoch);
    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, perhapsSetRTC(RTCQualityFromNet, &tv));

    struct timeval second = makeTv(firstEpoch + 500);
    TEST_ASSERT_EQUAL_INT(RTCSetResultNotSet, perhapsSetRTC(RTCQualityFromNet, &second));
    TEST_ASSERT_EQUAL_INT(RTCQualityFromNet, getRTCQuality());
    TEST_ASSERT_EQUAL_UINT32((uint32_t)firstEpoch, getTime());
}

// Our own GPS is authoritative: a GPS-quality set is always applied, with no throttle.
static void test_gps_reapply_always_accepted(void)
{
    beginFakeClock(60 * 1000);
    const time_t firstEpoch = makeValidEpoch();
    struct timeval tv = makeTv(firstEpoch);
    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, perhapsSetRTC(RTCQualityGPS, &tv));

    struct timeval second = makeTv(firstEpoch + 123);
    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, perhapsSetRTC(RTCQualityGPS, &second));
    TEST_ASSERT_EQUAL_INT(RTCQualityGPS, getRTCQuality());
    TEST_ASSERT_EQUAL_UINT32((uint32_t)firstEpoch + 123, getTime());
}

// Equal-quality NTP reapplies only after the 30-minute drift-correction throttle.
static void test_ntp_drift_throttle(void)
{
    beginFakeClock(120 * 1000);
    const time_t firstEpoch = makeValidEpoch();
    struct timeval tv = makeTv(firstEpoch);
    // The upgrade from None stamps the (function-static, not reset by resetRTCStateForTests)
    // throttle timestamp at a known fake instant, keeping this test order-independent.
    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, perhapsSetRTC(RTCQualityNTP, &tv));

    advanceFakeClock(10 * 60 * 1000); // +10 min: still inside the throttle window
    struct timeval second = makeTv(firstEpoch + 900);
    TEST_ASSERT_EQUAL_INT(RTCSetResultNotSet, perhapsSetRTC(RTCQualityNTP, &second));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)firstEpoch + 600, getTime());

    advanceFakeClock(21 * 60 * 1000); // total +31 min: past the window
    struct timeval third = makeTv(firstEpoch + 2000);
    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, perhapsSetRTC(RTCQualityNTP, &third));
    TEST_ASSERT_EQUAL_INT(RTCQualityNTP, getRTCQuality());
    TEST_ASSERT_EQUAL_UINT32((uint32_t)firstEpoch + 2000, getTime());
}

// forceUpdate applies the incoming time even when it is a quality downgrade - the T-Watch
// RTC-pause workaround depends on this override.
static void test_force_update_overrides_downgrade(void)
{
    beginFakeClock(60 * 1000);
    const time_t gpsEpoch = makeValidEpoch();
    struct timeval tv = makeTv(gpsEpoch);
    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, perhapsSetRTC(RTCQualityGPS, &tv));

    struct timeval forced = makeTv(gpsEpoch + 42);
    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, perhapsSetRTC(RTCQualityDevice, &forced, true));
    TEST_ASSERT_EQUAL_INT(RTCQualityDevice, getRTCQuality());
    TEST_ASSERT_EQUAL_UINT32((uint32_t)gpsEpoch + 42, getTime());
}

// The BUILD_EPOCH validity window rejects implausible epochs before quality arbitration - even
// with forceUpdate - and leaves state untouched. BUILD_EPOCH is not visible to this TU (see
// kFortyYears above), so probe at runtime whether RTC.cpp was built with the window enabled.
static void test_build_epoch_bounds_rejected(void)
{
    beginFakeClock(60 * 1000);
    const time_t gpsEpoch = makeValidEpoch();
    struct timeval tv = makeTv(gpsEpoch);
    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, perhapsSetRTC(RTCQualityGPS, &tv));

    struct timeval ancient = makeTv(1000000); // Jan 1970: below any plausible build epoch
    RTCSetResult probe = perhapsSetRTC(RTCQualityGPS, &ancient);
    if (probe == RTCSetResultSuccess) {
        TEST_IGNORE_MESSAGE("BUILD_EPOCH not defined in the RTC.cpp build; validity window inactive");
    }
    TEST_ASSERT_EQUAL_INT(RTCSetResultInvalidTime, probe);
    TEST_ASSERT_EQUAL_INT(RTCQualityGPS, getRTCQuality());
    TEST_ASSERT_EQUAL_UINT32((uint32_t)gpsEpoch, getTime());

    // BUILD_EPOCH <= time(NULL) at run time, so this is strictly beyond BUILD_EPOCH + FORTY_YEARS.
    struct timeval far = makeTv((time_t)((uint64_t)time(NULL) + kFortyYears + 2 * SEC_PER_DAY));
    TEST_ASSERT_EQUAL_INT(RTCSetResultInvalidTime, perhapsSetRTC(RTCQualityGPS, &far));

    // The window is checked before the forceUpdate override: force cannot smuggle in garbage.
    TEST_ASSERT_EQUAL_INT(RTCSetResultInvalidTime, perhapsSetRTC(RTCQualityGPS, &ancient, true));
    TEST_ASSERT_EQUAL_INT(RTCQualityGPS, getRTCQuality());
    TEST_ASSERT_EQUAL_UINT32((uint32_t)gpsEpoch, getTime());
}

// --- perhapsSetRTC(tm) overload ---

// The tm overload converts via gm_mktime and lands on the timeval path: a valid broken-down UTC
// time round-trips to the exact epoch (host gmtime() is the independent inverse).
static void test_tm_overload_roundtrip(void)
{
    beginFakeClock(60 * 1000);
    const time_t epoch = makeValidEpoch();
    struct tm t = *gmtime(&epoch);

    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, perhapsSetRTC(RTCQualityGPS, t));
    TEST_ASSERT_EQUAL_INT(RTCQualityGPS, getRTCQuality());
    TEST_ASSERT_EQUAL_UINT32((uint32_t)epoch, getTime());
}

// Implausible years are rejected with state untouched. On BUILD_EPOCH builds the validity window
// fires first, on windowless builds the tm_year guard (<0 or >=300) does; either way the caller
// must see RTCSetResultInvalidTime.
static void test_tm_overload_year_guard(void)
{
    beginFakeClock(60 * 1000);
    const time_t gpsEpoch = makeValidEpoch();
    struct timeval tv = makeTv(gpsEpoch);
    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, perhapsSetRTC(RTCQualityGPS, &tv));

    struct tm farFuture = {};
    farFuture.tm_year = 300; // year 2200
    farFuture.tm_mon = 5;
    farFuture.tm_mday = 15;
    TEST_ASSERT_EQUAL_INT(RTCSetResultInvalidTime, perhapsSetRTC(RTCQualityGPS, farFuture));

    struct tm preEpoch = {};
    preEpoch.tm_year = -5; // year 1895
    preEpoch.tm_mon = 0;
    preEpoch.tm_mday = 1;
    TEST_ASSERT_EQUAL_INT(RTCSetResultInvalidTime, perhapsSetRTC(RTCQualityGPS, preEpoch));

    TEST_ASSERT_EQUAL_INT(RTCQualityGPS, getRTCQuality());
    TEST_ASSERT_EQUAL_UINT32((uint32_t)gpsEpoch, getTime());
}

// --- getValidTime() threshold gating ---

static void test_getvalidtime_threshold_gating(void)
{
    beginFakeClock(60 * 1000);
    TEST_ASSERT_EQUAL_UINT32(0, getValidTime(RTCQualityDevice));
    TEST_ASSERT_EQUAL_UINT32(0, getValidTime(RTCQualityFromNet));

    const time_t netEpoch = makeValidEpoch();
    struct timeval tv = makeTv(netEpoch);
    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, perhapsSetRTC(RTCQualityFromNet, &tv));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)netEpoch, getValidTime(RTCQualityFromNet));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)netEpoch, getValidTime(RTCQualityDevice)); // at-or-below passes
    TEST_ASSERT_EQUAL_UINT32(0, getValidTime(RTCQualityNTP));
    TEST_ASSERT_EQUAL_UINT32(0, getValidTime(RTCQualityGPS));

    struct timeval gps = makeTv(netEpoch + 60);
    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, perhapsSetRTC(RTCQualityGPS, &gps));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)netEpoch + 60, getValidTime(RTCQualityGPS));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)netEpoch + 60, getValidTime(RTCQualityNTP));
}

// --- lastSetFromPhoneNtpOrGps stamp ---

// Stamped only for quality >= NTP: this is the input PositionModule::hasQualityTimesource() uses
// to gate mesh-time acceptance, so a FromNet or Device set must never refresh it.
static void test_lastSetFromPhoneNtpOrGps_stamp(void)
{
    beginFakeClock(200 * 1000);
    TEST_ASSERT_EQUAL_UINT32(0, lastSetFromPhoneNtpOrGps);

    const time_t epoch = makeValidEpoch();
    struct timeval tv = makeTv(epoch);
    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, perhapsSetRTC(RTCQualityFromNet, &tv));
    TEST_ASSERT_EQUAL_UINT32(0, lastSetFromPhoneNtpOrGps); // FromNet does not stamp

    advanceFakeClock(1000);
    struct timeval ntp = makeTv(epoch + 1);
    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, perhapsSetRTC(RTCQualityNTP, &ntp));
    TEST_ASSERT_EQUAL_UINT32(201 * 1000, lastSetFromPhoneNtpOrGps);

    advanceFakeClock(2000);
    struct timeval net = makeTv(epoch + 3);
    TEST_ASSERT_EQUAL_INT(RTCSetResultNotSet, perhapsSetRTC(RTCQualityFromNet, &net));
    TEST_ASSERT_EQUAL_UINT32(201 * 1000, lastSetFromPhoneNtpOrGps); // rejection leaves the stamp

    advanceFakeClock(3000);
    struct timeval gps = makeTv(epoch + 6);
    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, perhapsSetRTC(RTCQualityGPS, &gps));
    TEST_ASSERT_EQUAL_UINT32(206 * 1000, lastSetFromPhoneNtpOrGps);

    // Device-quality set from a clean slate: applied, but still no stamp.
    resetRTCStateForTests();
    struct timeval dev = makeTv(epoch + 9);
    TEST_ASSERT_EQUAL_INT(RTCSetResultSuccess, perhapsSetRTC(RTCQualityDevice, &dev));
    TEST_ASSERT_EQUAL_UINT32(0, lastSetFromPhoneNtpOrGps);
}

// --- gm_mktime known answers ---

// Hardcoded expected epochs (no host timegm dependence). The native build compiles the hand-rolled
// UTC path (!MESHTASTIC_EXCLUDE_TZ), so these pin its leap-day and century rules directly.
static void test_gm_mktime_known_epochs(void)
{
    struct KnownAnswer {
        int year, mon1, mday, hour, min, sec; // human calendar: year AD, month 1-12
        int64_t expected;
    };
    static const KnownAnswer cases[] = {
        {1970, 1, 1, 0, 0, 0, 0LL},
        {1970, 3, 1, 0, 0, 0, 5097600LL},        // non-leap February
        {1972, 2, 29, 0, 0, 0, 68169600LL},      // first leap day after the epoch
        {1999, 12, 31, 23, 59, 59, 946684799LL}, // second before Y2K
        {2000, 1, 1, 0, 0, 0, 946684800LL},
        {2000, 2, 29, 12, 0, 0, 951825600LL}, // 400-year-rule leap day
        {2000, 3, 1, 0, 0, 0, 951868800LL},
        {2023, 2, 28, 23, 59, 59, 1677628799LL}, // last second of a non-leap February
        {2024, 2, 29, 0, 0, 0, 1709164800LL},
        {2024, 3, 1, 0, 0, 0, 1709251200LL},
        {2038, 1, 19, 3, 14, 7, 2147483647LL}, // INT32_MAX second
        {2038, 1, 19, 3, 14, 8, 2147483648LL}, // one past it: 64-bit time_t on native
        {2100, 2, 28, 0, 0, 0, 4107456000LL},  // 2100 is NOT leap (100-year rule)
        {2100, 3, 1, 0, 0, 0, 4107542400LL},
        {2400, 2, 29, 0, 0, 0, 13574563200LL}, // 2400 IS leap (400-year rule)
    };

    for (const KnownAnswer &c : cases) {
        struct tm t = {};
        t.tm_year = c.year - 1900;
        t.tm_mon = c.mon1 - 1;
        t.tm_mday = c.mday;
        t.tm_hour = c.hour;
        t.tm_min = c.min;
        t.tm_sec = c.sec;
        const int64_t got = (int64_t)gm_mktime(&t);
        if (got != c.expected) {
            TEST_MSG_FMT("gm_mktime(%04d-%02d-%02d %02d:%02d:%02d) = %lld, expected %lld", c.year, c.mon1, c.mday, c.hour, c.min,
                         c.sec, (long long)got, (long long)c.expected);
        }
        TEST_ASSERT_EQUAL_INT64(c.expected, got);
    }
}

// February length as seen by gm_mktime for the years around each leap rule: Mar 1 minus Feb 28
// is two days in a leap year and one day otherwise. Self-consistent, anchored by the known
// answers above.
static void test_gm_mktime_leap_rule_sweep(void)
{
    static const int leapYears[] = {1972, 2000, 2024, 2096, 2104, 2400}; // by-4 and by-400
    static const int nonLeapYears[] = {1970, 2023, 2100, 2200, 2300};    // odd years and by-100

    for (int year : leapYears) {
        struct tm feb28 = {}, mar1 = {};
        feb28.tm_year = year - 1900;
        feb28.tm_mon = 1;
        feb28.tm_mday = 28;
        mar1.tm_year = year - 1900;
        mar1.tm_mon = 2;
        mar1.tm_mday = 1;
        TEST_MSG_FMT("leap year %d", year);
        TEST_ASSERT_EQUAL_INT64(2 * SEC_PER_DAY, (int64_t)gm_mktime(&mar1) - (int64_t)gm_mktime(&feb28));
    }
    for (int year : nonLeapYears) {
        struct tm feb28 = {}, mar1 = {};
        feb28.tm_year = year - 1900;
        feb28.tm_mon = 1;
        feb28.tm_mday = 28;
        mar1.tm_year = year - 1900;
        mar1.tm_mon = 2;
        mar1.tm_mday = 1;
        TEST_MSG_FMT("non-leap year %d", year);
        TEST_ASSERT_EQUAL_INT64(SEC_PER_DAY, (int64_t)gm_mktime(&mar1) - (int64_t)gm_mktime(&feb28));
    }
}

void setup()
{
    delay(10);
    initializeTestEnvironment();

    UNITY_BEGIN();
    RUN_TEST(test_readFromRTC_preserves_better_network_time);
    RUN_TEST(test_readFromRTC_initializes_time_when_no_better_source);

    printf("\n=== perhapsSetRTC(timeval) quality arbitration ===\n");
    RUN_TEST(test_downgrade_rejected_state_untouched);
    RUN_TEST(test_equal_quality_fromnet_is_not_reapplied);
    RUN_TEST(test_gps_reapply_always_accepted);
    RUN_TEST(test_ntp_drift_throttle);
    RUN_TEST(test_force_update_overrides_downgrade);
    RUN_TEST(test_build_epoch_bounds_rejected);

    printf("\n=== perhapsSetRTC(tm) overload ===\n");
    RUN_TEST(test_tm_overload_roundtrip);
    RUN_TEST(test_tm_overload_year_guard);

    printf("\n=== getValidTime / quality-source stamp ===\n");
    RUN_TEST(test_getvalidtime_threshold_gating);
    RUN_TEST(test_lastSetFromPhoneNtpOrGps_stamp);

    printf("\n=== gm_mktime known answers ===\n");
    RUN_TEST(test_gm_mktime_known_epochs);
    RUN_TEST(test_gm_mktime_leap_rule_sweep);

    exit(UNITY_END());
}

void loop() {}
