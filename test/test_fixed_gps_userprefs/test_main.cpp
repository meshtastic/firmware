// A build-time fixed position (USERPREFS_FIXED_GPS) must land on disk before the boot save decision and behave like
// every other USERPREFS_ default (app set/remove wins later). Without the define the suite pins the vanilla contract.
#include "MeshTypes.h" // Include BEFORE TestUtil.h
#include "TestUtil.h"
#include "mesh/NodeDB.h"
#include "mesh/TypeConversions.h"
#include <unity.h>

#if defined(ARCH_PORTDUINO)
#define FG_TEST_ENTRY extern "C"
#else
#define FG_TEST_ENTRY
#endif

#if defined(USERPREFS_FIXED_GPS) && defined(USERPREFS_FIXED_GPS_LAT) && defined(USERPREFS_FIXED_GPS_LON)
#define FIXED_GPS_BAKED 1
#else
#define FIXED_GPS_BAKED 0
#endif

void setUp(void) {}
void tearDown(void) {}

namespace
{
constexpr int32_t kAppLat = 471234567; // 47.1234567
constexpr int32_t kAppLon = 84567890;  // 8.4567890
constexpr int32_t kAppAlt = 432;

#if FIXED_GPS_BAKED
constexpr int32_t kBakedLat = (int32_t)(USERPREFS_FIXED_GPS_LAT * 1e7);
constexpr int32_t kBakedLon = (int32_t)(USERPREFS_FIXED_GPS_LON * 1e7);
#ifdef USERPREFS_FIXED_GPS_ALT
constexpr int32_t kBakedAlt = USERPREFS_FIXED_GPS_ALT;
#endif
#endif

// A real reboot starts with an empty localPosition (process global) and a fresh NodeDB.
void rebootNodeDB()
{
    localPosition = meshtastic_Position_init_default;
    NodeDB *rebooted = new NodeDB();
    delete nodeDB;
    nodeDB = rebooted;
}

bool persistedFixedFlag()
{
    meshtastic_LocalConfig saved = meshtastic_LocalConfig_init_zero;
    TEST_ASSERT_EQUAL(LoadFileResult::LOAD_SUCCESS, nodeDB->loadProto(configFileName, meshtastic_LocalConfig_size, sizeof(saved),
                                                                      &meshtastic_LocalConfig_msg, &saved));
    return saved.position.fixed_position;
}

// True when nodes.proto on disk carries a non-zero position for our own node. Decodes into a scratch
// struct: with the decode targets disarmed the callback appends to the struct's own vector.
bool persistedSelfPosition(meshtastic_PositionLite &out)
{
    meshtastic_NodeDatabase saved;
    if (nodeDB->loadProto(nodeDatabaseFileName, nodeDB->getMaxNodesAllocatedSize(), sizeof(saved), &meshtastic_NodeDatabase_msg,
                          &saved) != LoadFileResult::LOAD_SUCCESS)
        return false;
    for (const auto &entry : saved.positions) {
        if (entry.num == nodeDB->getNodeNum() && entry.has_position) {
            out = entry.position;
            return out.latitude_i != 0 || out.longitude_i != 0;
        }
    }
    return false;
}

meshtastic_Position appPosition(int32_t lat, int32_t lon, int32_t alt)
{
    meshtastic_Position p = meshtastic_Position_init_default;
    p.latitude_i = lat;
    p.has_latitude_i = true;
    p.longitude_i = lon;
    p.has_longitude_i = true;
    p.altitude = alt;
    p.has_altitude = true;
    p.location_source = meshtastic_Position_LocSource_LOC_MANUAL;
    return p;
}

// Mirrors AdminModule set_fixed_position: what the phone app does.
void appSetsFixedPosition(const meshtastic_Position &p)
{
    nodeDB->updatePosition(nodeDB->getNodeNum(), p, RX_SRC_LOCAL);
    nodeDB->setLocalPosition(p);
    config.position.fixed_position = true;
    TEST_ASSERT_TRUE(nodeDB->saveToDisk(SEGMENT_NODEDATABASE | SEGMENT_CONFIG));
}

// Mirrors AdminModule remove_fixed_position.
void appRemovesFixedPosition()
{
    nodeDB->clearLocalPosition();
    config.position.fixed_position = false;
    TEST_ASSERT_TRUE(nodeDB->saveToDisk(SEGMENT_NODEDATABASE | SEGMENT_CONFIG));
}

void assertSelfPositionIs(int32_t lat, int32_t lon)
{
    meshtastic_PositionLite self;
    TEST_ASSERT_TRUE(nodeDB->copyNodePosition(nodeDB->getNodeNum(), self));
    TEST_ASSERT_EQUAL_INT32(lat, self.latitude_i);
    TEST_ASSERT_EQUAL_INT32(lon, self.longitude_i);
}
} // namespace

#if FIXED_GPS_BAKED

static void test_firstBoot_seedsAndPersistsBakedPosition(void)
{
    // In RAM after the very first boot ...
    TEST_ASSERT_TRUE(config.position.fixed_position);
    TEST_ASSERT_EQUAL_INT32(kBakedLat, localPosition.latitude_i);
    TEST_ASSERT_EQUAL_INT32(kBakedLon, localPosition.longitude_i);
#ifdef USERPREFS_FIXED_GPS_ALT
    TEST_ASSERT_EQUAL_INT32(kBakedAlt, localPosition.altitude);
#endif
    TEST_ASSERT_EQUAL(meshtastic_Position_LocSource_LOC_MANUAL, localPosition.location_source);
    TEST_ASSERT_TRUE(nodeDB->hasLocalPositionSinceBoot());
    assertSelfPositionIs(kBakedLat, kBakedLon);

    // ... and on disk, not just in RAM: the seed must land before the boot save decision.
    TEST_ASSERT_TRUE(persistedFixedFlag());
    meshtastic_PositionLite onDisk;
    TEST_ASSERT_TRUE(persistedSelfPosition(onDisk));
    TEST_ASSERT_EQUAL_INT32(kBakedLat, onDisk.latitude_i);
    TEST_ASSERT_EQUAL_INT32(kBakedLon, onDisk.longitude_i);
}

static void test_reboot_restoresBakedPosition(void)
{
    // A later boot is not the first boot: the position must come back from disk, not from luck.
    rebootNodeDB();

    TEST_ASSERT_TRUE(config.position.fixed_position);
    TEST_ASSERT_EQUAL_INT32(kBakedLat, localPosition.latitude_i);
    TEST_ASSERT_EQUAL_INT32(kBakedLon, localPosition.longitude_i);
    TEST_ASSERT_TRUE(nodeDB->hasLocalPositionSinceBoot());
    assertSelfPositionIs(kBakedLat, kBakedLon);
}

static void test_appOverride_survivesReboot(void)
{
    // The baked value is a default, not a lock: a fixed position set from the app wins on reboot.
    appSetsFixedPosition(appPosition(kAppLat, kAppLon, kAppAlt));
    rebootNodeDB();

    TEST_ASSERT_TRUE(config.position.fixed_position);
    TEST_ASSERT_EQUAL_INT32(kAppLat, localPosition.latitude_i);
    TEST_ASSERT_EQUAL_INT32(kAppLon, localPosition.longitude_i);
    assertSelfPositionIs(kAppLat, kAppLon);
}

static void test_lostSelfPosition_isReseededOnReboot(void)
{
    // fixed_position stays on but nodes.proto lost our row (self-care, a reset, a corrupt file):
    // the compiled-in coordinates come back instead of a node that silently stops reporting.
    nodeDB->clearLocalPosition(); // erases the self position, keeps fixed_position
    TEST_ASSERT_TRUE(nodeDB->saveToDisk(SEGMENT_NODEDATABASE));
    TEST_ASSERT_TRUE(config.position.fixed_position);
    rebootNodeDB();

    TEST_ASSERT_TRUE(config.position.fixed_position);
    TEST_ASSERT_EQUAL_INT32(kBakedLat, localPosition.latitude_i);
    TEST_ASSERT_EQUAL_INT32(kBakedLon, localPosition.longitude_i);
    assertSelfPositionIs(kBakedLat, kBakedLon);
    meshtastic_PositionLite onDisk;
    TEST_ASSERT_TRUE(persistedSelfPosition(onDisk));
    TEST_ASSERT_EQUAL_INT32(kBakedLat, onDisk.latitude_i);
}

static void test_appRemove_staysRemovedAfterReboot(void)
{
    // Same contract as every other USERPREFS_ default: the user's removal is respected on later boots.
    appRemovesFixedPosition();
    TEST_ASSERT_FALSE(persistedFixedFlag());
    rebootNodeDB();

    TEST_ASSERT_FALSE(config.position.fixed_position);
    TEST_ASSERT_EQUAL_INT32(0, localPosition.latitude_i);
    TEST_ASSERT_EQUAL_INT32(0, localPosition.longitude_i);
    TEST_ASSERT_FALSE(nodeDB->hasLocalPositionSinceBoot());
    TEST_ASSERT_FALSE(persistedFixedFlag());
}

#else // vanilla build: no USERPREFS_FIXED_GPS

static void test_vanillaBoot_hasNoPosition(void)
{
    TEST_ASSERT_FALSE(config.position.fixed_position);
    TEST_ASSERT_EQUAL_INT32(0, localPosition.latitude_i);
    TEST_ASSERT_EQUAL_INT32(0, localPosition.longitude_i);
    TEST_ASSERT_FALSE(nodeDB->hasLocalPositionSinceBoot());
    TEST_ASSERT_FALSE(persistedFixedFlag());
}

static void test_appFixedPosition_isRestoredAfterReboot(void)
{
    appSetsFixedPosition(appPosition(kAppLat, kAppLon, kAppAlt));
    TEST_ASSERT_TRUE(persistedFixedFlag());
    rebootNodeDB();

    TEST_ASSERT_TRUE(config.position.fixed_position);
    TEST_ASSERT_EQUAL_INT32(kAppLat, localPosition.latitude_i);
    TEST_ASSERT_EQUAL_INT32(kAppLon, localPosition.longitude_i);
    TEST_ASSERT_TRUE(nodeDB->hasLocalPositionSinceBoot());
    assertSelfPositionIs(kAppLat, kAppLon);
}

static void test_appRemove_staysRemovedAfterReboot(void)
{
    appRemovesFixedPosition();
    rebootNodeDB();

    TEST_ASSERT_FALSE(config.position.fixed_position);
    TEST_ASSERT_EQUAL_INT32(0, localPosition.latitude_i);
    TEST_ASSERT_EQUAL_INT32(0, localPosition.longitude_i);
    TEST_ASSERT_FALSE(persistedFixedFlag());
}

#endif

FG_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    nodeDB = new NodeDB();

    UNITY_BEGIN();
#if FIXED_GPS_BAKED
    RUN_TEST(test_firstBoot_seedsAndPersistsBakedPosition);
    RUN_TEST(test_reboot_restoresBakedPosition);
    RUN_TEST(test_appOverride_survivesReboot);
    RUN_TEST(test_lostSelfPosition_isReseededOnReboot);
    RUN_TEST(test_appRemove_staysRemovedAfterReboot);
#else
    RUN_TEST(test_vanillaBoot_hasNoPosition);
    RUN_TEST(test_appFixedPosition_isRestoredAfterReboot);
    RUN_TEST(test_appRemove_staysRemovedAfterReboot);
#endif
    exit(UNITY_END());
}
FG_TEST_ENTRY void loop() {}
