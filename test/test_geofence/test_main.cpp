#include "TestUtil.h"
#include "modules/GeofenceModule.h"
#include <unity.h>

// These exercise GeofenceModule's pure, side-effect-free geofence helpers (the inside test and the
// crossing classifier). They take plain values / proto structs, so no device globals or fake clock
// are needed. The (waypointId, nodeNum) store and notification plumbing are not covered here.

using Crossing = GeofenceModule::Crossing;

// 0.001 deg of longitude at the equator is ~111 m; 0.001 deg = 10000 in degrees x 1e-7 units.
static const int32_t kLon111m = 10000;

static void test_insideRadius_centreIsInside()
{
    TEST_ASSERT_TRUE(GeofenceModule::insideRadius(123456, 654321, 123456, 654321, 10));
}

static void test_insideRadius_withinRadius()
{
    // ~111 m east of the centre, radius 200 m -> inside.
    TEST_ASSERT_TRUE(GeofenceModule::insideRadius(0, kLon111m, 0, 0, 200));
}

static void test_insideRadius_outsideRadius()
{
    // ~111 m east of the centre, radius 50 m -> outside.
    TEST_ASSERT_FALSE(GeofenceModule::insideRadius(0, kLon111m, 0, 0, 50));
}

static void test_insideRadius_zeroRadiusNeverInside()
{
    // radius 0 means "no circle" -> never inside, even exactly at the centre.
    TEST_ASSERT_FALSE(GeofenceModule::insideRadius(123456, 654321, 123456, 654321, 0));
}

static meshtastic_BoundingBox makeBox()
{
    meshtastic_BoundingBox box = meshtastic_BoundingBox_init_zero;
    box.longitude_west_i = -1000;
    box.latitude_south_i = -2000;
    box.longitude_east_i = 1000;
    box.latitude_north_i = 2000;
    return box;
}

static void test_insideBox_centreInside()
{
    TEST_ASSERT_TRUE(GeofenceModule::insideBox(0, 0, makeBox()));
}

static void test_insideBox_edgesInclusive()
{
    meshtastic_BoundingBox box = makeBox();
    TEST_ASSERT_TRUE(GeofenceModule::insideBox(box.latitude_north_i, box.longitude_east_i, box));
    TEST_ASSERT_TRUE(GeofenceModule::insideBox(box.latitude_south_i, box.longitude_west_i, box));
}

static void test_insideBox_outsideLat()
{
    TEST_ASSERT_FALSE(GeofenceModule::insideBox(2001, 0, makeBox()));
}

static void test_insideBox_outsideLon()
{
    TEST_ASSERT_FALSE(GeofenceModule::insideBox(0, 1001, makeBox()));
}

static void test_inside_circleOnly()
{
    meshtastic_Waypoint wp = meshtastic_Waypoint_init_zero;
    wp.latitude_i = 0;
    wp.longitude_i = 0;
    wp.geofence_radius = 200;
    wp.has_bounding_box = false;
    TEST_ASSERT_TRUE(GeofenceModule::inside(wp, 0, kLon111m));
    TEST_ASSERT_FALSE(GeofenceModule::inside(wp, 0, kLon111m * 4)); // ~444 m east
}

static void test_inside_boxOnly()
{
    meshtastic_Waypoint wp = meshtastic_Waypoint_init_zero;
    wp.geofence_radius = 0;
    wp.has_bounding_box = true;
    wp.bounding_box = makeBox();
    TEST_ASSERT_TRUE(GeofenceModule::inside(wp, 0, 0));
    TEST_ASSERT_FALSE(GeofenceModule::inside(wp, 5000, 0));
}

static void test_inside_eitherShapeCounts()
{
    // Circle is tiny (point far from centre is outside it) but the box still contains the point.
    meshtastic_Waypoint wp = meshtastic_Waypoint_init_zero;
    wp.geofence_radius = 1; // 1 m circle
    wp.has_bounding_box = true;
    wp.bounding_box = makeBox();
    TEST_ASSERT_TRUE(GeofenceModule::inside(wp, 1500, 0)); // outside circle, inside box
}

static void test_inside_noGeofenceNeverInside()
{
    meshtastic_Waypoint wp = meshtastic_Waypoint_init_zero;
    wp.geofence_radius = 0;
    wp.has_bounding_box = false;
    TEST_ASSERT_FALSE(GeofenceModule::inside(wp, 0, 0));
    TEST_ASSERT_FALSE(GeofenceModule::hasGeofence(wp));
}

static meshtastic_Waypoint makeNotifyingWaypoint()
{
    meshtastic_Waypoint wp = meshtastic_Waypoint_init_zero;
    wp.notify_on_enter = true;
    return wp;
}

static void test_shouldTrack_circleWithCentre()
{
    meshtastic_Waypoint wp = makeNotifyingWaypoint();
    wp.geofence_radius = 100;
    wp.has_latitude_i = true;
    wp.has_longitude_i = true;
    TEST_ASSERT_TRUE(GeofenceModule::shouldTrack(wp, 0));
}

static void test_shouldTrack_circleWithoutCentreRejected()
{
    meshtastic_Waypoint wp = makeNotifyingWaypoint();
    wp.geofence_radius = 100; // circle needs a centre
    wp.has_latitude_i = false;
    wp.has_longitude_i = false;
    TEST_ASSERT_FALSE(GeofenceModule::shouldTrack(wp, 0));
}

static void test_shouldTrack_boxOnlyWithoutCentreAccepted()
{
    // A box-only geofence carries absolute corners, so it needs no waypoint centre.
    meshtastic_Waypoint wp = makeNotifyingWaypoint();
    wp.geofence_radius = 0;
    wp.has_bounding_box = true;
    wp.bounding_box = makeBox();
    wp.has_latitude_i = false;
    wp.has_longitude_i = false;
    TEST_ASSERT_TRUE(GeofenceModule::shouldTrack(wp, 0));
}

static void test_shouldTrack_noGeofenceRejected()
{
    meshtastic_Waypoint wp = makeNotifyingWaypoint(); // notify set, but no geofence shape
    TEST_ASSERT_FALSE(GeofenceModule::shouldTrack(wp, 0));
}

static void test_shouldTrack_noNotifyFlagsRejected()
{
    meshtastic_Waypoint wp = meshtastic_Waypoint_init_zero;
    wp.geofence_radius = 100;
    wp.has_latitude_i = true;
    wp.has_longitude_i = true;
    // notify_on_enter / notify_on_exit both false -> nothing to alert on.
    TEST_ASSERT_FALSE(GeofenceModule::shouldTrack(wp, 0));
}

static void test_shouldTrack_expiredRejectedButLiveAccepted()
{
    meshtastic_Waypoint wp = makeNotifyingWaypoint();
    wp.geofence_radius = 100;
    wp.has_latitude_i = true;
    wp.has_longitude_i = true;
    wp.expire = 1000;
    TEST_ASSERT_FALSE(GeofenceModule::shouldTrack(wp, 2000)); // expire <= now -> expired
    TEST_ASSERT_TRUE(GeofenceModule::shouldTrack(wp, 500));   // expire > now -> live
    TEST_ASSERT_TRUE(GeofenceModule::shouldTrack(wp, 0));     // no clock -> treat as live
}

static void test_classify_firstSightingNeverNotifies()
{
    // First sighting only baselines, regardless of inside state or notify flags.
    TEST_ASSERT_TRUE(GeofenceModule::classify(true, false, true, true, true) == Crossing::None);
    TEST_ASSERT_TRUE(GeofenceModule::classify(true, false, false, true, true) == Crossing::None);
}

static void test_classify_noTransitionNeverNotifies()
{
    TEST_ASSERT_TRUE(GeofenceModule::classify(false, true, true, true, true) == Crossing::None);
    TEST_ASSERT_TRUE(GeofenceModule::classify(false, false, false, true, true) == Crossing::None);
}

static void test_classify_enterFiresOnlyWhenEnabled()
{
    TEST_ASSERT_TRUE(GeofenceModule::classify(false, false, true, true, false) == Crossing::Enter);
    TEST_ASSERT_TRUE(GeofenceModule::classify(false, false, true, false, false) == Crossing::None);
}

static void test_classify_exitFiresOnlyWhenEnabled()
{
    TEST_ASSERT_TRUE(GeofenceModule::classify(false, true, false, false, true) == Crossing::Exit);
    TEST_ASSERT_TRUE(GeofenceModule::classify(false, true, false, false, false) == Crossing::None);
}

static void test_classifyTrackedUpdate_firstInsideUsesPreviousOutside()
{
    TEST_ASSERT_TRUE(
        GeofenceModule::classifyTrackedUpdate(false, false, true, false, true, true, true) == Crossing::Enter);
}

static void test_classifyTrackedUpdate_firstOutsideUsesPreviousInside()
{
    TEST_ASSERT_TRUE(
        GeofenceModule::classifyTrackedUpdate(false, false, true, true, false, true, true) == Crossing::Exit);
}

static void test_classifyTrackedUpdate_noPreviousStillBaselines()
{
    TEST_ASSERT_TRUE(
        GeofenceModule::classifyTrackedUpdate(false, false, false, false, true, true, true) == Crossing::None);
}

void setUp(void) {}

void tearDown(void) {}

extern "C" {
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_insideRadius_centreIsInside);
    RUN_TEST(test_insideRadius_withinRadius);
    RUN_TEST(test_insideRadius_outsideRadius);
    RUN_TEST(test_insideRadius_zeroRadiusNeverInside);
    RUN_TEST(test_insideBox_centreInside);
    RUN_TEST(test_insideBox_edgesInclusive);
    RUN_TEST(test_insideBox_outsideLat);
    RUN_TEST(test_insideBox_outsideLon);
    RUN_TEST(test_inside_circleOnly);
    RUN_TEST(test_inside_boxOnly);
    RUN_TEST(test_inside_eitherShapeCounts);
    RUN_TEST(test_inside_noGeofenceNeverInside);
    RUN_TEST(test_shouldTrack_circleWithCentre);
    RUN_TEST(test_shouldTrack_circleWithoutCentreRejected);
    RUN_TEST(test_shouldTrack_boxOnlyWithoutCentreAccepted);
    RUN_TEST(test_shouldTrack_noGeofenceRejected);
    RUN_TEST(test_shouldTrack_noNotifyFlagsRejected);
    RUN_TEST(test_shouldTrack_expiredRejectedButLiveAccepted);
    RUN_TEST(test_classify_firstSightingNeverNotifies);
    RUN_TEST(test_classify_noTransitionNeverNotifies);
    RUN_TEST(test_classify_enterFiresOnlyWhenEnabled);
    RUN_TEST(test_classify_exitFiresOnlyWhenEnabled);
    RUN_TEST(test_classifyTrackedUpdate_firstInsideUsesPreviousOutside);
    RUN_TEST(test_classifyTrackedUpdate_firstOutsideUsesPreviousInside);
    RUN_TEST(test_classifyTrackedUpdate_noPreviousStillBaselines);
    exit(UNITY_END());
}

void loop() {}
}
