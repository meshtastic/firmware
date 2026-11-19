// Unit tests for setGgaAltitude() in src/gps/GPS.cpp - how a GGA sentence's altitude and geoid
// separation become the altitude fields of our own Position.
//
// PositionModule copies each altitude field's presence into the broadcast, so the presence set here
// is what the mesh sees. TinyGPS++ commits a GGA field only when the sentence carried it, which
// makes isValid() the honest presence bit: a sentence with an empty altitude field has no altitude,
// and one that reports 0.0 m does.
//
// Regressions guarded:
//  - presence left unset, which drops altitude from every GPS node's broadcast once PositionModule
//    stops forcing it on;
//  - presence forced on, which broadcasts a receiver's missing altitude as a reading of 0 m;
//  - a reported 0.0 m treated as missing, which loses a real sea-level reading.
#include "Arduino.h"
#include "TestUtil.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <TinyGPS++.h>
#include <unity.h>

void setGgaAltitude(meshtastic_Position &p, TinyGPSAltitude &altitude, TinyGPSAltitude &geoid);

static meshtastic_Position fromSentence(const char *sentence)
{
    TinyGPSPlus reader;
    for (const char *c = sentence; *c; c++)
        reader.encode(*c);
    meshtastic_Position p = meshtastic_Position_init_default;
    setGgaAltitude(p, reader.altitude, reader.geoidHeight);
    return p;
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
    RUN_TEST(test_gga_at_sea_level_is_a_reading);
    exit(UNITY_END());
}

void loop() {}
}
