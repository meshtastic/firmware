// Unit tests for gpsMatchAnyIdentity() in src/gps/GPS.cpp - the probe-cache identity match.
//
// On a warm boot the firmware re-verifies its cached GPS model by pinging the receiver and looking for
// the model's name in the reply. Several models answer to more than one name: GNSS_MODEL_CM121 covers
// both "CM121" and "CC1167Q", and GNSS_MODEL_UC6580 covers "UC6580" and "UM600". The old code matched a
// single string, so a board carrying the other name failed verification on every warm boot and survived
// only via the passive sawNmeaSentenceAtBaud() fallback. Measured on a ThinkNode M9, whose receiver
// answers "$PDTINFO,CC1167Q,G1B1,N/A,N1000R3.52.1.3269Build15172,N/A,N/A": getACK("CM121", 900) returned
// GNSS_RESPONSE_NONE every time.
//
// The UC6580 case additionally chained two getACK() calls after a single write, which cannot work: the
// first consumes the reply and the second waits out its whole timeout against an empty port. Matching
// several candidates in ONE pass is what fixes both, so this is the predicate worth covering.
#include "Arduino.h"
#include "TestUtil.h"
#include <cstdint>
#include <cstring>
#include <unity.h>

// Lives beside its only caller in src/gps/GPS.cpp, which the native build compiles; a signature change
// breaks the link rather than silently diverging.
int gpsMatchAnyIdentity(const char *buf, size_t len, const char *const *messages, size_t count);

static const char *const kCm121[] = {"CM121", "CC1167Q"};
static const char *const kUc6580[] = {"UC6580", "UM600"};

// The real reply from the bench's ThinkNode M9.
static const char *kCc1167qReply = "$PDTINFO,CC1167Q,G1B1,N/A,N1000R3.52.1.3269Build15172,N/A,N/A*65\r\n";

void test_matches_the_second_candidate(void)
{
    // The case that was broken: the chip answers with the name that was not being looked for.
    TEST_ASSERT_EQUAL_INT(1, gpsMatchAnyIdentity(kCc1167qReply, strlen(kCc1167qReply), kCm121, 2));
}

void test_matches_the_first_candidate(void)
{
    const char *reply = "$PDTINFO,CM121,G1B1,N/A,R3.52,N/A,N/A*00\r\n";
    TEST_ASSERT_EQUAL_INT(0, gpsMatchAnyIdentity(reply, strlen(reply), kCm121, 2));
}

void test_um600_matches_without_consuming_a_second_read(void)
{
    // One pass, both candidates: a UM600 reply must match even though "UC6580" is tried first.
    const char *reply = "$PDTINFO,UM600,,,R4.0,,*00\r\n";
    TEST_ASSERT_EQUAL_INT(1, gpsMatchAnyIdentity(reply, strlen(reply), kUc6580, 2));
}

void test_unrelated_reply_does_not_match(void)
{
    const char *reply = "$PDTINFO,AG3335,,,,,*00\r\n";
    TEST_ASSERT_EQUAL_INT(-1, gpsMatchAnyIdentity(reply, strlen(reply), kCm121, 2));
}

void test_respects_the_length_limit(void)
{
    // The name is present in the buffer but beyond the bytes actually read, so it must not match:
    // getACKAny() calls this with bytesRead, not the buffer size.
    TEST_ASSERT_EQUAL_INT(-1, gpsMatchAnyIdentity(kCc1167qReply, 9, kCm121, 2));
}

void test_tolerates_null_entries_and_empty_lists(void)
{
    static const char *const withNull[] = {nullptr, "CC1167Q"};
    TEST_ASSERT_EQUAL_INT(1, gpsMatchAnyIdentity(kCc1167qReply, strlen(kCc1167qReply), withNull, 2));
    TEST_ASSERT_EQUAL_INT(-1, gpsMatchAnyIdentity(kCc1167qReply, strlen(kCc1167qReply), kCm121, 0));
    TEST_ASSERT_EQUAL_INT(-1, gpsMatchAnyIdentity(nullptr, 10, kCm121, 2));
    TEST_ASSERT_EQUAL_INT(-1, gpsMatchAnyIdentity(kCc1167qReply, strlen(kCc1167qReply), nullptr, 2));
}

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_matches_the_second_candidate);
    RUN_TEST(test_matches_the_first_candidate);
    RUN_TEST(test_um600_matches_without_consuming_a_second_read);
    RUN_TEST(test_unrelated_reply_does_not_match);
    RUN_TEST(test_respects_the_length_limit);
    RUN_TEST(test_tolerates_null_entries_and_empty_lists);
    exit(UNITY_END());
}

void loop() {}
