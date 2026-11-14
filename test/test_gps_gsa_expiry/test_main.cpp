// Unit tests for gpsDatumFresh() in src/gps/GPS.cpp - the rule that decides whether a GSA-derived
// value may still be used.
//
// A TinyGPS++ datum latches isValid() the first time it is set and never clears it; only age()
// grows. So "the receiver told us this once" and "the receiver is still telling us this" are
// different questions, and every consumer of a sentence that can stop arriving has to ask the
// second one.
//
// The regression this guards: GSA is not sent by every receiver, and a receiver can stop sending
// it mid-session - the firmware's own setup writes a sentence mask that switches GSA off on the
// MTK families, and an MTK3339 emits GSA by default before that write lands. When validity alone
// was trusted, fixType stayed at its last value with an ever-growing age, which (a) froze
// hasLock() on a stale 1 or 2, and (b) made the location freshness check reject every otherwise
// good GGA/RMC solution from then on. The receiver kept a fix; the firmware reported none, for the
// rest of the session.
#include "Arduino.h"
#include "TestUtil.h"
#include <cstdint>
#include <unity.h>

// Lives beside its only callers in src/gps/GPS.cpp rather than in a header; the native test build
// compiles that file, so a prototype here is enough, and a signature change breaks the link
// instead of silently diverging.
bool gpsDatumFresh(bool isValid, uint32_t age);

// GPS_SOL_EXPIRY_MS, spelled out so this suite does not pull in GPS.h and its hardware deps.
static constexpr uint32_t kExpiry = 5000;

void setUp(void) {}
void tearDown(void) {}

// A datum the receiver has never sent is not usable, whatever its age reads as.
void test_an_invalid_datum_is_never_fresh(void)
{
    TEST_ASSERT_FALSE(gpsDatumFresh(false, 0));
    TEST_ASSERT_FALSE(gpsDatumFresh(false, kExpiry - 1));
    TEST_ASSERT_FALSE(gpsDatumFresh(false, kExpiry + 1));
}

void test_a_just_arrived_datum_is_fresh(void)
{
    TEST_ASSERT_TRUE(gpsDatumFresh(true, 0));
}

// The boundary is exclusive, matching the location/time/date checks beside it.
void test_the_expiry_boundary_is_exclusive(void)
{
    TEST_ASSERT_TRUE_MESSAGE(gpsDatumFresh(true, kExpiry - 1), "one millisecond inside the window is still usable");
    TEST_ASSERT_FALSE_MESSAGE(gpsDatumFresh(true, kExpiry), "at the expiry it is stale, as for every other datum here");
}

// The regression. This is the state a receiver leaves behind when it stops sending a sentence:
// still valid, arbitrarily old. Returning true here is what froze hasLock() and rejected every
// location update for the rest of the session.
void test_a_sentence_that_stopped_arriving_goes_stale(void)
{
    TEST_ASSERT_FALSE_MESSAGE(gpsDatumFresh(true, kExpiry * 2), "a valid-but-old datum must read as absent, not as last seen");
    TEST_ASSERT_FALSE_MESSAGE(gpsDatumFresh(true, 10u * 60u * 1000u), "ten minutes without the sentence is not a measurement");
}

// age() is unsigned and TinyGPS++ computes it as millis() - lastCommitTime, so it wraps rather
// than going negative. A wrapped age is enormous, which this must read as stale, not as fresh.
void test_a_wrapped_age_reads_as_stale(void)
{
    TEST_ASSERT_FALSE(gpsDatumFresh(true, UINT32_MAX));
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_an_invalid_datum_is_never_fresh);
    RUN_TEST(test_a_just_arrived_datum_is_fresh);
    RUN_TEST(test_the_expiry_boundary_is_exclusive);
    RUN_TEST(test_a_sentence_that_stopped_arriving_goes_stale);
    RUN_TEST(test_a_wrapped_age_reads_as_stale);
    exit(UNITY_END());
}

void loop() {}
