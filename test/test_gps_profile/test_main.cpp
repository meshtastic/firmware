#include "GpsProfile.h"
#include "TestUtil.h"

#include <climits>
#include <unity.h>

namespace
{
constexpr uint32_t kBaseFlags =
    meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE | meshtastic_Config_PositionConfig_PositionFlags_DOP |
    meshtastic_Config_PositionConfig_PositionFlags_SATINVIEW | meshtastic_Config_PositionConfig_PositionFlags_TIMESTAMP;

meshtastic_Config_PositionConfig positionWithSentinelValues()
{
    meshtastic_Config_PositionConfig position = {};
    position.gps_update_interval = 321;
    position.position_broadcast_secs = 654;
    position.broadcast_smart_minimum_interval_secs = 987;
    position.broadcast_smart_minimum_distance = 1234;
    position.position_flags = meshtastic_Config_PositionConfig_PositionFlags_HVDOP;
    position.position_broadcast_smart_enabled = false;
    return position;
}
} // namespace

void test_manual_profile_preserves_existing_position_settings()
{
    auto position = positionWithSentinelValues();
    position.gps_profile = meshtastic_Config_PositionConfig_GpsProfile_MANUAL;

    TEST_ASSERT_FALSE(GpsProfile::apply(position));
    TEST_ASSERT_EQUAL_UINT32(321, position.gps_update_interval);
    TEST_ASSERT_EQUAL_UINT32(654, position.position_broadcast_secs);
    TEST_ASSERT_EQUAL_UINT32(987, position.broadcast_smart_minimum_interval_secs);
    TEST_ASSERT_EQUAL_UINT32(1234, position.broadcast_smart_minimum_distance);
    TEST_ASSERT_EQUAL_UINT32(meshtastic_Config_PositionConfig_PositionFlags_HVDOP, position.position_flags);
    TEST_ASSERT_FALSE(position.position_broadcast_smart_enabled);
}

void test_vehicle_profile_uses_documented_policy()
{
    auto position = positionWithSentinelValues();
    position.gps_profile = meshtastic_Config_PositionConfig_GpsProfile_VEHICLE;

    TEST_ASSERT_TRUE(GpsProfile::apply(position));
    TEST_ASSERT_EQUAL_UINT32(30, position.gps_update_interval);
    TEST_ASSERT_EQUAL_UINT32(900, position.position_broadcast_secs);
    TEST_ASSERT_EQUAL_UINT32(120, position.broadcast_smart_minimum_interval_secs);
    TEST_ASSERT_EQUAL_UINT32(500, position.broadcast_smart_minimum_distance);
    TEST_ASSERT_EQUAL_UINT32(kBaseFlags | meshtastic_Config_PositionConfig_PositionFlags_SPEED |
                                 meshtastic_Config_PositionConfig_PositionFlags_HEADING,
                             position.position_flags);
    TEST_ASSERT_TRUE(position.position_broadcast_smart_enabled);
}

void test_pedestrian_profile_uses_documented_policy()
{
    auto position = positionWithSentinelValues();
    position.gps_profile = meshtastic_Config_PositionConfig_GpsProfile_PEDESTRIAN;

    TEST_ASSERT_TRUE(GpsProfile::apply(position));
    TEST_ASSERT_EQUAL_UINT32(60, position.gps_update_interval);
    TEST_ASSERT_EQUAL_UINT32(900, position.position_broadcast_secs);
    TEST_ASSERT_EQUAL_UINT32(60, position.broadcast_smart_minimum_interval_secs);
    TEST_ASSERT_EQUAL_UINT32(50, position.broadcast_smart_minimum_distance);
    TEST_ASSERT_EQUAL_UINT32(kBaseFlags | meshtastic_Config_PositionConfig_PositionFlags_SPEED |
                                 meshtastic_Config_PositionConfig_PositionFlags_HEADING,
                             position.position_flags);
    TEST_ASSERT_TRUE(position.position_broadcast_smart_enabled);
}

void test_airborne_profile_uses_documented_policy()
{
    auto position = positionWithSentinelValues();
    position.gps_profile = meshtastic_Config_PositionConfig_GpsProfile_AIRBORNE;

    TEST_ASSERT_TRUE(GpsProfile::apply(position));
    TEST_ASSERT_EQUAL_UINT32(10, position.gps_update_interval);
    TEST_ASSERT_EQUAL_UINT32(900, position.position_broadcast_secs);
    TEST_ASSERT_EQUAL_UINT32(60, position.broadcast_smart_minimum_interval_secs);
    TEST_ASSERT_EQUAL_UINT32(2000, position.broadcast_smart_minimum_distance);
    TEST_ASSERT_EQUAL_UINT32(kBaseFlags | meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE_MSL |
                                 meshtastic_Config_PositionConfig_PositionFlags_GEOIDAL_SEPARATION |
                                 meshtastic_Config_PositionConfig_PositionFlags_SPEED |
                                 meshtastic_Config_PositionConfig_PositionFlags_HEADING,
                             position.position_flags);
    TEST_ASSERT_TRUE(position.position_broadcast_smart_enabled);
}

void test_fixed_profile_disables_smart_broadcasting()
{
    auto position = positionWithSentinelValues();
    position.gps_profile = meshtastic_Config_PositionConfig_GpsProfile_FIXED_POSITION;

    TEST_ASSERT_TRUE(GpsProfile::apply(position));
    TEST_ASSERT_EQUAL_UINT32(1800, position.gps_update_interval);
    TEST_ASSERT_EQUAL_UINT32(3600, position.position_broadcast_secs);
    TEST_ASSERT_EQUAL_UINT32(INT32_MAX, position.broadcast_smart_minimum_interval_secs);
    TEST_ASSERT_EQUAL_UINT32(INT32_MAX, position.broadcast_smart_minimum_distance);
    TEST_ASSERT_EQUAL_UINT32(kBaseFlags, position.position_flags);
    TEST_ASSERT_FALSE(position.position_broadcast_smart_enabled);
}

void test_ublox_dynamic_model_matches_profile()
{
    uint8_t dynamicModel = 0;

    TEST_ASSERT_TRUE(GpsProfile::ubxDynamicModel(meshtastic_Config_PositionConfig_GpsProfile_FIXED_POSITION, dynamicModel));
    TEST_ASSERT_EQUAL_UINT8(2, dynamicModel);
    TEST_ASSERT_TRUE(GpsProfile::ubxDynamicModel(meshtastic_Config_PositionConfig_GpsProfile_PEDESTRIAN, dynamicModel));
    TEST_ASSERT_EQUAL_UINT8(3, dynamicModel);
    TEST_ASSERT_TRUE(GpsProfile::ubxDynamicModel(meshtastic_Config_PositionConfig_GpsProfile_VEHICLE, dynamicModel));
    TEST_ASSERT_EQUAL_UINT8(4, dynamicModel);
    TEST_ASSERT_TRUE(GpsProfile::ubxDynamicModel(meshtastic_Config_PositionConfig_GpsProfile_AIRBORNE, dynamicModel));
    TEST_ASSERT_EQUAL_UINT8(7, dynamicModel);
    TEST_ASSERT_FALSE(GpsProfile::ubxDynamicModel(meshtastic_Config_PositionConfig_GpsProfile_MANUAL, dynamicModel));
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_manual_profile_preserves_existing_position_settings);
    RUN_TEST(test_vehicle_profile_uses_documented_policy);
    RUN_TEST(test_pedestrian_profile_uses_documented_policy);
    RUN_TEST(test_airborne_profile_uses_documented_policy);
    RUN_TEST(test_fixed_profile_disables_smart_broadcasting);
    RUN_TEST(test_ublox_dynamic_model_matches_profile);
    exit(UNITY_END());
}

void loop() {}
