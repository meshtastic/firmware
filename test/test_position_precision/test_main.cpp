#include "PositionPrecision.h"
#include "TestUtil.h"
#include "mesh-pb-constants.h"
#include <unity.h>

static meshtastic_Position makePosition()
{
    meshtastic_Position position = meshtastic_Position_init_default;
    position.has_latitude_i = true;
    position.latitude_i = static_cast<int32_t>(0x12345678);
    position.has_longitude_i = true;
    position.longitude_i = static_cast<int32_t>(0x22345678);
    position.has_altitude = true;
    position.altitude = 123;
    position.time = 42;
    position.location_source = meshtastic_Position_LocSource_LOC_EXTERNAL;
    position.timestamp = 43;
    position.sats_in_view = 10;
    return position;
}

static meshtastic_Channel makeChannel(meshtastic_Channel_Role role, bool hasModuleSettings, uint32_t positionPrecision)
{
    meshtastic_Channel channel = meshtastic_Channel_init_default;
    channel.has_settings = true;
    channel.role = role;
    channel.settings.has_module_settings = hasModuleSettings;
    channel.settings.module_settings.position_precision = positionPrecision;
    return channel;
}

static void test_applyPositionPrecision_clampsLatLonAndSetsPrecisionBits()
{
    meshtastic_Position position = makePosition();

    applyPositionPrecision(position, 16);

    TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(0x12348000), position.latitude_i);
    TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(0x22348000), position.longitude_i);
    TEST_ASSERT_EQUAL_UINT32(16, position.precision_bits);
    TEST_ASSERT_TRUE(position.has_latitude_i);
    TEST_ASSERT_TRUE(position.has_longitude_i);
}

static void test_applyPositionPrecision_fullPrecisionKeepsLatLon()
{
    meshtastic_Position position = makePosition();

    applyPositionPrecision(position, 32);

    TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(0x12345678), position.latitude_i);
    TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(0x22345678), position.longitude_i);
    TEST_ASSERT_EQUAL_UINT32(32, position.precision_bits);
}

static void test_applyPositionPrecision_zeroScrubsLocationButKeepsTime()
{
    meshtastic_Position position = makePosition();

    applyPositionPrecision(position, 0);

    TEST_ASSERT_FALSE(position.has_latitude_i);
    TEST_ASSERT_EQUAL_INT32(0, position.latitude_i);
    TEST_ASSERT_FALSE(position.has_longitude_i);
    TEST_ASSERT_EQUAL_INT32(0, position.longitude_i);
    TEST_ASSERT_FALSE(position.has_altitude);
    TEST_ASSERT_EQUAL_INT32(0, position.altitude);
    TEST_ASSERT_EQUAL_UINT32(42, position.time);
    TEST_ASSERT_EQUAL_UINT32(0, position.timestamp);
    TEST_ASSERT_EQUAL_UINT32(0, position.sats_in_view);
    TEST_ASSERT_EQUAL_UINT32(0, position.precision_bits);
}

static void test_applyPositionPrecision_reencodesPositionPacket()
{
    meshtastic_Position position = makePosition();
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_default;
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    packet.decoded.portnum = meshtastic_PortNum_POSITION_APP;
    packet.decoded.payload.size = pb_encode_to_bytes(packet.decoded.payload.bytes, sizeof(packet.decoded.payload.bytes),
                                                     &meshtastic_Position_msg, &position);

    TEST_ASSERT_TRUE(applyPositionPrecision(packet, 16));

    meshtastic_Position decoded = meshtastic_Position_init_default;
    TEST_ASSERT_TRUE(
        pb_decode_from_bytes(packet.decoded.payload.bytes, packet.decoded.payload.size, &meshtastic_Position_msg, &decoded));
    TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(0x12348000), decoded.latitude_i);
    TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(0x22348000), decoded.longitude_i);
    TEST_ASSERT_EQUAL_UINT32(16, decoded.precision_bits);
}

static void test_getPositionPrecisionForChannel_explicitPrecisionIsHonored()
{
    meshtastic_Channel channel = makeChannel(meshtastic_Channel_Role_PRIMARY, true, 16);

    TEST_ASSERT_EQUAL_UINT32(16, getPositionPrecisionForChannel(channel));
}

static void test_getPositionPrecisionForChannel_explicitZeroDisablesPrimary()
{
    meshtastic_Channel channel = makeChannel(meshtastic_Channel_Role_PRIMARY, true, 0);

    TEST_ASSERT_EQUAL_UINT32(0, getPositionPrecisionForChannel(channel));
}

static void test_getPositionPrecisionForChannel_primaryWithoutModuleSettingsFailsClosed()
{
    // Regression guard for #10509: precision 32 below must be ignored (no module settings).
    meshtastic_Channel channel = makeChannel(meshtastic_Channel_Role_PRIMARY, false, 32);

    TEST_ASSERT_EQUAL_UINT32(0, getPositionPrecisionForChannel(channel));
}

static void test_getPositionPrecisionForChannel_secondaryWithoutModuleSettingsFailsClosed()
{
    meshtastic_Channel channel = makeChannel(meshtastic_Channel_Role_SECONDARY, false, 32);

    TEST_ASSERT_EQUAL_UINT32(0, getPositionPrecisionForChannel(channel));
}

void setUp(void) {}

void tearDown(void) {}

extern "C" {
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_applyPositionPrecision_clampsLatLonAndSetsPrecisionBits);
    RUN_TEST(test_applyPositionPrecision_fullPrecisionKeepsLatLon);
    RUN_TEST(test_applyPositionPrecision_zeroScrubsLocationButKeepsTime);
    RUN_TEST(test_applyPositionPrecision_reencodesPositionPacket);
    RUN_TEST(test_getPositionPrecisionForChannel_explicitPrecisionIsHonored);
    RUN_TEST(test_getPositionPrecisionForChannel_explicitZeroDisablesPrimary);
    RUN_TEST(test_getPositionPrecisionForChannel_primaryWithoutModuleSettingsFailsClosed);
    RUN_TEST(test_getPositionPrecisionForChannel_secondaryWithoutModuleSettingsFailsClosed);
    exit(UNITY_END());
}

void loop() {}
}
