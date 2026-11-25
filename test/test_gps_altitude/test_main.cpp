// Unit tests for setGgaAltitude() in src/gps/GPS.cpp - how a GGA sentence's altitude and geoid
// separation become the altitude fields of our own Position.
//
// PositionModule copies each altitude field's presence into the broadcast, so the presence set here
// is what the mesh sees. TinyGPS++ commits a GGA field only when the sentence carried it, but keeps
// the last committed value valid forever, so presence is the age of that commit against the solution
// window: a sentence with an empty altitude field has no altitude once the old one ages out, and one
// that reports 0.0 m does.
//
// Regressions guarded:
//  - presence left unset, which drops altitude from every GPS node's broadcast once PositionModule
//    stops forcing it on;
//  - presence forced on, which broadcasts a receiver's missing altitude as a reading of 0 m;
//  - presence from isValid(), which keeps sending an altitude the receiver stopped reporting;
//  - a reported 0.0 m treated as missing, which loses a real sea-level reading.
#include "Arduino.h"
#include "TestUtil.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <TinyGPS++.h>
#include <unity.h>

void setGgaAltitude(meshtastic_Position &p, TinyGPSAltitude &altitude, TinyGPSAltitude &geoid, uint32_t maxAgeMs);

// GPS_SOL_EXPIRY_MS, spelled out so the suite does not pull in GPS.h and its hardware deps.
static constexpr uint32_t kExpiryMs = 5000;

static const char *const kWithAltitude = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
static const char *const kNoAltitude = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,,M,,M,,*7C\r\n";

static void feed(TinyGPSPlus &reader, const char *sentence)
{
    for (const char *c = sentence; *c; c++)
        reader.encode(*c);
}

static meshtastic_Position altitudeOf(TinyGPSPlus &reader, uint32_t maxAgeMs)
{
    meshtastic_Position p = meshtastic_Position_init_default;
    setGgaAltitude(p, reader.altitude, reader.geoidHeight, maxAgeMs);
    return p;
}

static meshtastic_Position fromSentence(const char *sentence)
{
    TinyGPSPlus reader;
    feed(reader, sentence);
    return altitudeOf(reader, kExpiryMs);
}

void setUp(void) {}
void tearDown(void) {}

static void test_gga_with_altitude_and_geoid_sets_all_three(void)
{
    const meshtastic_Position p = fromSentence("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n");
    TEST_ASSERT_TRUE(p.has_altitude);
    TEST_ASSERT_TRUE(p.has_altitude_hae);
    TEST_ASSERT_TRUE(p.has_altitude_geoidal_separation);
    TEST_ASSERT_EQUAL_INT32(545, p.altitude);
    TEST_ASSERT_EQUAL_INT32(46, p.altitude_geoidal_separation);
    // HAE adds the already-truncated geoid separation to the MSL reading.
    TEST_ASSERT_EQUAL_INT32(591, p.altitude_hae);
}

static void test_gga_without_altitude_sets_none(void)
{
    const meshtastic_Position p = fromSentence("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,,M,,M,,*7C\r\n");
    TEST_ASSERT_FALSE(p.has_altitude);
    TEST_ASSERT_FALSE(p.has_altitude_hae);
    TEST_ASSERT_FALSE(p.has_altitude_geoidal_separation);
}

// A receiver that reports no geoid separation still has an altitude; HAE falls back to the MSL value.
static void test_gga_without_geoid_keeps_altitude(void)
{
    const meshtastic_Position p = fromSentence("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,,M,,*52\r\n");
    TEST_ASSERT_TRUE(p.has_altitude);
    TEST_ASSERT_TRUE(p.has_altitude_hae);
    TEST_ASSERT_FALSE(p.has_altitude_geoidal_separation);
    TEST_ASSERT_EQUAL_INT32(545, p.altitude_hae);
}

// TinyGPS++ keeps a field valid after a later sentence leaves it empty, so presence follows the age of the last
// commit: an altitude stops being sent once no sentence has carried one for longer than the window.
static void test_gga_empty_after_present_expires(void)
{
    TinyGPSPlus reader;
    feed(reader, kWithAltitude);
    TEST_ASSERT_TRUE(altitudeOf(reader, 20).has_altitude);
    delay(50);
    feed(reader, kNoAltitude);
    const meshtastic_Position p = altitudeOf(reader, 20);
    TEST_ASSERT_FALSE(p.has_altitude);
    TEST_ASSERT_FALSE(p.has_altitude_hae);
    TEST_ASSERT_FALSE(p.has_altitude_geoidal_separation);
}

static void test_gga_at_sea_level_is_a_reading(void)
{
    const meshtastic_Position p = fromSentence("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,0.0,M,46.9,M,,*47\r\n");
    TEST_ASSERT_TRUE(p.has_altitude);
    TEST_ASSERT_EQUAL_INT32(0, p.altitude);
}

extern "C" {
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_gga_with_altitude_and_geoid_sets_all_three);
    RUN_TEST(test_gga_without_altitude_sets_none);
    RUN_TEST(test_gga_without_geoid_keeps_altitude);
    RUN_TEST(test_gga_empty_after_present_expires);
    RUN_TEST(test_gga_at_sea_level_is_a_reading);
    exit(UNITY_END());
}

void loop() {}
}
