#include "GeoCoord.h"
#include "NMEAWPL.h"
#include "TestUtil.h"
#include "mesh-pb-constants.h"
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

static meshtastic_PositionLite makePositionLite()
{
    meshtastic_PositionLite pos = meshtastic_PositionLite_init_default;
    pos.latitude_i = 472852133;
    pos.longitude_i = 85652500;
    pos.altitude = 400;
    pos.time = 42;
    return pos;
}

static meshtastic_Position makePosition()
{
    meshtastic_Position pos = meshtastic_Position_init_default;
    pos.has_latitude_i = true;
    pos.latitude_i = 472852133;
    pos.has_longitude_i = true;
    pos.longitude_i = 85652500;
    pos.has_altitude = true;
    pos.altitude = 400;
    pos.timestamp = 43;
    return pos;
}

static uint32_t expectedChecksum(const char *sentence)
{
    uint32_t chk = 0;
    const char *c = strchr(sentence, '$');
    TEST_ASSERT_NOT_NULL(c);
    for (c++; *c && *c != '*'; c++)
        chk ^= (uint8_t)*c;
    return chk;
}

static uint32_t emittedChecksum(const char *buf)
{
    const char *star = strrchr(buf, '*');
    TEST_ASSERT_NOT_NULL(star);
    TEST_ASSERT_TRUE(isxdigit((unsigned char)star[1]));
    TEST_ASSERT_TRUE(isxdigit((unsigned char)star[2]));
    TEST_ASSERT_TRUE(star[3] == '\r' || star[3] == '\0');
    unsigned parsed = 0;
    TEST_ASSERT_EQUAL_INT(1, sscanf(star + 1, "%02X", &parsed));
    return parsed;
}

static void assertChecksumMatchesBody(const char *buf)
{
    TEST_ASSERT_EQUAL_UINT32(expectedChecksum(buf), emittedChecksum(buf));
}

void test_wpl_lite_checksum_skips_leading_crlf(void)
{
    char buf[128];
    meshtastic_PositionLite pos = makePositionLite();
    uint32_t len = printWPL(buf, sizeof(buf), pos, "Test", false);

    TEST_ASSERT_TRUE(len < sizeof(buf));
    TEST_ASSERT_EQUAL_CHAR('\r', buf[0]);
    TEST_ASSERT_EQUAL_CHAR('\n', buf[1]);
    TEST_ASSERT_EQUAL_CHAR('$', buf[2]);
    assertChecksumMatchesBody(buf);
}

void test_wpl_position_checksum(void)
{
    char buf[128];
    meshtastic_Position pos = makePosition();
    uint32_t len = printWPL(buf, sizeof(buf), pos, "Test", false);

    TEST_ASSERT_TRUE(len < sizeof(buf));
    TEST_ASSERT_EQUAL_CHAR('$', buf[0]);
    assertChecksumMatchesBody(buf);
}

void test_gga_checksum(void)
{
    char buf[160];
    meshtastic_Position pos = makePosition();
    uint32_t len = printGGA(buf, sizeof(buf), pos);

    TEST_ASSERT_TRUE(len < sizeof(buf));
    TEST_ASSERT_EQUAL_CHAR('$', buf[0]);
    assertChecksumMatchesBody(buf);
}

void test_crlf_prefix_does_not_change_checksum(void)
{
    const uint32_t fixtureChecksum = 0x69;
    char withPrefix[128];
    char withoutPrefix[128];
    meshtastic_PositionLite lite = makePositionLite();
    meshtastic_Position pos = makePosition();

    printWPL(withPrefix, sizeof(withPrefix), lite, "Test", false);
    printWPL(withoutPrefix, sizeof(withoutPrefix), pos, "Test", false);

    TEST_ASSERT_EQUAL_UINT32(fixtureChecksum, emittedChecksum(withPrefix));
    TEST_ASSERT_EQUAL_UINT32(fixtureChecksum, emittedChecksum(withoutPrefix));
}

void test_zero_sized_buffer_writes_nothing(void)
{
    char buf[64];
    memset(buf, 'A', sizeof(buf));
    meshtastic_PositionLite lite = makePositionLite();
    meshtastic_Position pos = makePosition();

    TEST_ASSERT_EQUAL_UINT32(0, printWPL(buf, 0, lite, "Test", false));
    TEST_ASSERT_EQUAL_UINT32(0, printWPL(buf, 0, pos, "Test", false));
    TEST_ASSERT_EQUAL_UINT32(0, printGGA(buf, 0, pos));

    for (size_t i = 0; i < sizeof(buf); i++)
        TEST_ASSERT_EQUAL_CHAR('A', buf[i]);
}

void test_truncated_buffers_stay_in_bounds(void)
{
    const size_t sizes[] = {1, 2, 8, 20, 40};
    meshtastic_PositionLite lite = makePositionLite();

    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        char buf[128];
        memset(buf, 0x7E, sizeof(buf));
        uint32_t len = printWPL(buf, sizes[s], lite, "Test", false);

        TEST_ASSERT_TRUE(len < sizes[s]);
        for (size_t i = sizes[s]; i < sizeof(buf); i++)
            TEST_ASSERT_EQUAL_HEX8(0x7E, (uint8_t)buf[i]);
    }
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_wpl_lite_checksum_skips_leading_crlf);
    RUN_TEST(test_wpl_position_checksum);
    RUN_TEST(test_gga_checksum);
    RUN_TEST(test_crlf_prefix_does_not_change_checksum);
    RUN_TEST(test_zero_sized_buffer_writes_nothing);
    RUN_TEST(test_truncated_buffers_stay_in_bounds);
    exit(UNITY_END());
}

void loop() {}
