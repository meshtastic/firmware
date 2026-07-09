// Adversarial fuzzing of the NMEA/WPL serial-output formatters (printWPL / printGGA) - the sink the
// SerialModule NMEA/CALTOPO mode feeds a *received* Position into.
//
// printWPL(const meshtastic_Position&) is reached with an attacker-controlled Position: in NMEA/CALTOPO
// serial mode a received POSITION_APP packet is decoded and formatted to a fixed outbuf[90]
// (SerialModule.cpp). printGGA and printWPL(const meshtastic_PositionLite&) format the *local* node's
// position, so they are not RF-reachable, but share the same formatter and are covered here for
// robustness. GeoCoord's UTM/MGRS out-of-bounds on extreme coordinates is regression-tested separately
// in test_position_precision; this suite covers the formatter layer on top of it:
//   * extreme int32 lat/lon can never overflow the fixed output buffer,
//   * the output is always NUL-terminated strictly inside the buffer (incl. the snprintf-truncation
//     path, which pre-hardening let `bufsz - len` underflow into an out-of-bounds write), and
//   * the `name` argument (formatted with %s) is never read past its allocation.
//
// Runs under the default `coverage` env (AddressSanitizer + LeakSanitizer); any out-of-bounds
// read/write or leak on adversarial input turns the run RED. Inputs come from a deterministic seeded
// LCG so a failure always reproduces from the printed seed.

#include "MeshTypes.h" // include BEFORE TestUtil.h
#include "TestUtil.h"
#include <unity.h>

#include "mesh/generated/meshtastic/mesh.pb.h"
#include "support/DeterministicRng.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if !MESHTASTIC_EXCLUDE_GPS

// printWPL/printGGA live in src/gps/NMEAWPL.cpp (compiled under the coverage env - GPS is not excluded
// there). Forward-declared here so the test does not pull gps/NMEAWPL.h -> main.h -> <Arduino.h>.
uint32_t printWPL(char *buf, size_t bufsz, const meshtastic_Position &pos, const char *name, bool isCaltopoMode);
uint32_t printWPL(char *buf, size_t bufsz, const meshtastic_PositionLite &pos, const char *name, bool isCaltopoMode);
uint32_t printGGA(char *buf, size_t bufsz, const meshtastic_Position &pos);

static constexpr uint64_t BASE_SEED = 0x00E3A000ULL; // distinct from the other fuzz suites' seeds

// The extreme int32 coordinate corners a received Position can carry (raw sfixed32, unvalidated on
// decode) - the same corner set as test_position_precision's GeoCoord OOB regression.
static const int32_t kExtremeCoords[] = {INT32_MIN,  INT32_MAX,   INT32_MIN + 1, INT32_MAX - 1, 0, 1, -1, 900000000,
                                         -900000000,              // +/- 90 deg
                                         1800000000, -1800000000, // +/- 180 deg
                                         2000000000, -2000000000, 123456789,     -123456789};
static constexpr size_t kNumExtreme = sizeof(kExtremeCoords) / sizeof(kExtremeCoords[0]);

// Every snprintf-family formatter must leave the buffer NUL-terminated strictly inside its size.
static void assertBounded(const char *buf, size_t bufsz)
{
    TEST_ASSERT_TRUE_MESSAGE(strnlen(buf, bufsz) < bufsz, "NMEA output not NUL-terminated within the buffer");
}

// N1 - printWPL over every extreme coordinate pair, both overloads, into the real 90-byte outbuf size
// plus deliberately tiny buffers that force the snprintf-truncation path (exercises the len>=bufsz
// clamp that guards the checksum loop and the trailing snprintf against a size_t underflow).
void test_N1_printWPL_extreme_coords(void)
{
    uint64_t seed = BASE_SEED ^ 0x11;
    rngSeed(seed);
    printf("  seed=0x%llx\n", (unsigned long long)seed);

    const size_t bufSizes[] = {90, 4, 8, 16, 32, 128};
    for (size_t bi = 0; bi < sizeof(bufSizes) / sizeof(bufSizes[0]); bi++) {
        const size_t bufsz = bufSizes[bi];
        for (size_t i = 0; i < kNumExtreme; i++) {
            for (size_t j = 0; j < kNumExtreme; j++) {
                char buf[128];
                const bool caltopo = (rngRange(2) == 0);

                meshtastic_Position p = meshtastic_Position_init_zero;
                p.latitude_i = kExtremeCoords[i];
                p.longitude_i = kExtremeCoords[j];
                p.altitude = (int32_t)rngNext();
                memset(buf, 0xAA, sizeof(buf));
                printWPL(buf, bufsz, p, "fuzznode", caltopo);
                assertBounded(buf, bufsz);

                meshtastic_PositionLite pl = meshtastic_PositionLite_init_zero;
                pl.latitude_i = kExtremeCoords[i];
                pl.longitude_i = kExtremeCoords[j];
                pl.altitude = (int32_t)rngNext();
                memset(buf, 0xAA, sizeof(buf));
                printWPL(buf, bufsz, pl, "fuzznode", caltopo);
                assertBounded(buf, bufsz);
            }
        }
    }
    TEST_ASSERT_TRUE(true); // reaching here = no ASan fault
}

// N2 - printGGA over extreme coordinates (local-only sink; robustness of the shared formatter). The
// timestamp is kept inside a range gmtime() can represent so the test does not manufacture an
// out-of-RF-scope gmtime()->NULL path; the target under test here is coordinate/altitude formatting.
void test_N2_printGGA_extreme_coords(void)
{
    uint64_t seed = BASE_SEED ^ 0x22;
    rngSeed(seed);
    printf("  seed=0x%llx\n", (unsigned long long)seed);

    const size_t bufSizes[] = {90, 8, 16, 128};
    for (size_t bi = 0; bi < sizeof(bufSizes) / sizeof(bufSizes[0]); bi++) {
        const size_t bufsz = bufSizes[bi];
        for (size_t i = 0; i < kNumExtreme; i++) {
            for (size_t j = 0; j < kNumExtreme; j++) {
                char buf[128];
                meshtastic_Position p = meshtastic_Position_init_zero;
                p.latitude_i = kExtremeCoords[i];
                p.longitude_i = kExtremeCoords[j];
                p.altitude = (int32_t)rngNext();
                p.timestamp = (uint32_t)rngRange(4102444800u); // 0 .. year 2100, valid for gmtime
                p.timestamp_millis_adjust = (int32_t)rngRange(1000);
                p.fix_quality = (uint32_t)rngRange(256);
                p.sats_in_view = (uint32_t)rngRange(256);
                p.HDOP = (uint32_t)rngNext();
                p.altitude_geoidal_separation = (int32_t)rngNext();
                memset(buf, 0xAA, sizeof(buf));
                printGGA(buf, bufsz, p);
                assertBounded(buf, bufsz);
            }
        }
    }
    TEST_ASSERT_TRUE(true);
}

// N3 - the `name` argument is formatted with %s; prove it is never read past its allocation. Production
// callers always pass a NUL-terminated long_name (char[40]); here we place a terminated name in an
// exact-sized heap block so ASan's redzone would catch any over-read past the terminator.
void test_N3_printWPL_name_heap_bounds(void)
{
    uint64_t seed = BASE_SEED ^ 0x33;
    rngSeed(seed);
    printf("  seed=0x%llx\n", (unsigned long long)seed);

    for (unsigned k = 0; k < 4000; k++) {
        size_t n = rngRange(40); // content length 0..39 (long_name is char[40])
        char *name = (char *)malloc(n + 1);
        for (size_t i = 0; i < n; i++)
            name[i] = (char)(1 + rngRange(255)); // non-NUL content
        name[n] = '\0';

        meshtastic_Position p = meshtastic_Position_init_zero;
        p.latitude_i = (int32_t)rngNext();
        p.longitude_i = (int32_t)rngNext();
        p.altitude = (int32_t)rngNext();

        char buf[90];
        printWPL(buf, sizeof(buf), p, name, rngRange(2) == 0);
        assertBounded(buf, sizeof(buf));
        free(name);
    }
    TEST_ASSERT_TRUE(true);
}

// ---------------------------------------------------------------------------
void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    printf("\n=== Group N: NMEA/WPL formatter fuzz ===\n");
    RUN_TEST(test_N1_printWPL_extreme_coords);
    RUN_TEST(test_N2_printGGA_extreme_coords);
    RUN_TEST(test_N3_printWPL_name_heap_bounds);
    exit(UNITY_END());
}

void loop() {}

#else // MESHTASTIC_EXCLUDE_GPS - no NMEA formatters to exercise; compile an empty passing suite.

void setUp(void) {}
void tearDown(void) {}
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}
void loop() {}

#endif
