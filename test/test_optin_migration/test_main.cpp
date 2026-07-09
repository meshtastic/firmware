// Unit tests for the 2.8 position/telemetry opt-in migration helpers (NodeDB.cpp / Channels.cpp).
//
// Covers:
//   - channelFileUsesPublicKey(): the public/private-PSK classification the migration keys off.
//   - optInDisablePositionSharing(): zero precision on PUBLIC channels, preserve PRIVATE channels.
//   - optInDisableTelemetryBroadcast(): force every mesh-broadcast telemetry flag + map-report location off.
//
// The helpers are pure field mutators, so they run against locally-built structs with no filesystem,
// singleton, or radio setup. The one-time version-gate + saveToDisk() around them lives in
// NodeDB::loadFromDisk() and is exercised by the hardware/MCP verification, not here.

#include "MeshTypes.h" // Include BEFORE TestUtil.h
#include "TestUtil.h"
#include <unity.h>

#include "mesh/Channels.h"
#include "mesh/NodeDB.h"
#include <cstring>
#include <initializer_list>

namespace
{

// A private 128-bit key that is NOT in the defaultpsk family.
const uint8_t kPrivate16[16] = {0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB};
// A private 256-bit key.
const uint8_t kPrivate32[32] = {0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD,
                                0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD};
// The single-byte well-known key index (the stock primary default).
const uint8_t kSingle1[1] = {1};

meshtastic_Channel makeCh(meshtastic_Channel_Role role, const uint8_t *psk, size_t pskLen, uint32_t precision,
                          bool hasModuleSettings = true)
{
    meshtastic_Channel ch = meshtastic_Channel_init_default;
    ch.has_settings = true;
    ch.role = role;
    ch.settings.psk.size = (pb_size_t)pskLen;
    if (pskLen)
        memcpy(ch.settings.psk.bytes, psk, pskLen);
    ch.settings.has_module_settings = hasModuleSettings;
    ch.settings.module_settings.position_precision = precision;
    return ch;
}

meshtastic_ChannelFile makeFile(std::initializer_list<meshtastic_Channel> chs, uint32_t version = 24)
{
    meshtastic_ChannelFile cf = meshtastic_ChannelFile_init_default;
    cf.version = version;
    cf.channels_count = 0;
    for (const meshtastic_Channel &c : chs)
        cf.channels[cf.channels_count++] = c;
    return cf;
}

// ---- channelFileUsesPublicKey ---------------------------------------------------------------------

void test_publicKey_openPrimaryIsPublic()
{
    meshtastic_ChannelFile cf = makeFile({makeCh(meshtastic_Channel_Role_PRIMARY, nullptr, 0, 13)});
    TEST_ASSERT_TRUE(channelFileUsesPublicKey(cf, 0));
}

void test_publicKey_singleByteIsPublic()
{
    meshtastic_ChannelFile cf = makeFile({makeCh(meshtastic_Channel_Role_PRIMARY, kSingle1, 1, 13)});
    TEST_ASSERT_TRUE(channelFileUsesPublicKey(cf, 0));
}

void test_publicKey_defaultpskFamilyIsPublic()
{
    meshtastic_ChannelFile cf = makeFile({makeCh(meshtastic_Channel_Role_PRIMARY, defaultpsk, sizeof(defaultpsk), 13)});
    TEST_ASSERT_TRUE(channelFileUsesPublicKey(cf, 0));
}

void test_publicKey_private16IsPrivate()
{
    meshtastic_ChannelFile cf = makeFile({makeCh(meshtastic_Channel_Role_PRIMARY, kPrivate16, 16, 13)});
    TEST_ASSERT_FALSE(channelFileUsesPublicKey(cf, 0));
}

void test_publicKey_private32IsPrivate()
{
    meshtastic_ChannelFile cf = makeFile({makeCh(meshtastic_Channel_Role_PRIMARY, kPrivate32, 32, 13)});
    TEST_ASSERT_FALSE(channelFileUsesPublicKey(cf, 0));
}

void test_publicKey_disabledIsNotPublic()
{
    meshtastic_ChannelFile cf = makeFile({makeCh(meshtastic_Channel_Role_DISABLED, kSingle1, 1, 13)});
    TEST_ASSERT_FALSE(channelFileUsesPublicKey(cf, 0));
}

void test_publicKey_secondaryInheritsPrivatePrimary()
{
    // Secondary with no PSK inherits the PRIMARY's key -> private primary makes it private.
    meshtastic_ChannelFile cf = makeFile(
        {makeCh(meshtastic_Channel_Role_PRIMARY, kPrivate16, 16, 0), makeCh(meshtastic_Channel_Role_SECONDARY, nullptr, 0, 13)});
    TEST_ASSERT_FALSE(channelFileUsesPublicKey(cf, 1));
}

void test_publicKey_secondaryInheritsPublicPrimary()
{
    meshtastic_ChannelFile cf = makeFile(
        {makeCh(meshtastic_Channel_Role_PRIMARY, kSingle1, 1, 0), makeCh(meshtastic_Channel_Role_SECONDARY, nullptr, 0, 13)});
    TEST_ASSERT_TRUE(channelFileUsesPublicKey(cf, 1));
}

// ---- optInDisablePositionSharing ------------------------------------------------------------------

void test_optIn_zeroesPublicPreservesPrivate()
{
    meshtastic_ChannelFile cf = makeFile({
        makeCh(meshtastic_Channel_Role_PRIMARY, kSingle1, 1, 13),      // public default -> 0
        makeCh(meshtastic_Channel_Role_SECONDARY, kPrivate16, 16, 13), // private -> preserved
        makeCh(meshtastic_Channel_Role_SECONDARY, kSingle1, 1, 13),    // public secondary -> 0
    });

    optInDisablePositionSharing(cf);

    TEST_ASSERT_EQUAL_UINT32(0, cf.channels[0].settings.module_settings.position_precision);
    TEST_ASSERT_EQUAL_UINT32(13, cf.channels[1].settings.module_settings.position_precision); // preserved
    TEST_ASSERT_EQUAL_UINT32(0, cf.channels[2].settings.module_settings.position_precision);
}

void test_optIn_zeroesOpenChannel()
{
    meshtastic_ChannelFile cf = makeFile({makeCh(meshtastic_Channel_Role_PRIMARY, nullptr, 0, 13)});
    optInDisablePositionSharing(cf);
    TEST_ASSERT_EQUAL_UINT32(0, cf.channels[0].settings.module_settings.position_precision);
}

void test_optIn_secondaryUnderPrivatePreserved()
{
    meshtastic_ChannelFile cf = makeFile(
        {makeCh(meshtastic_Channel_Role_PRIMARY, kPrivate16, 16, 0), makeCh(meshtastic_Channel_Role_SECONDARY, nullptr, 0, 13)});
    optInDisablePositionSharing(cf);
    TEST_ASSERT_EQUAL_UINT32(13, cf.channels[1].settings.module_settings.position_precision); // preserved
}

void test_optIn_secondaryUnderPublicZeroed()
{
    meshtastic_ChannelFile cf = makeFile(
        {makeCh(meshtastic_Channel_Role_PRIMARY, kSingle1, 1, 0), makeCh(meshtastic_Channel_Role_SECONDARY, nullptr, 0, 13)});
    optInDisablePositionSharing(cf);
    TEST_ASSERT_EQUAL_UINT32(0, cf.channels[1].settings.module_settings.position_precision);
}

void test_optIn_setsHasModuleSettingsOnZeroed()
{
    // A public channel that never had module_settings still ends up explicitly off (has_module_settings=true).
    meshtastic_ChannelFile cf = makeFile({makeCh(meshtastic_Channel_Role_PRIMARY, kSingle1, 1, 13, /*hasModule*/ false)});
    optInDisablePositionSharing(cf);
    TEST_ASSERT_TRUE(cf.channels[0].settings.has_module_settings);
    TEST_ASSERT_EQUAL_UINT32(0, cf.channels[0].settings.module_settings.position_precision);
}

// ---- optInDisableTelemetryBroadcast ---------------------------------------------------------------

void test_optIn_telemetryAllFlagsOff()
{
    meshtastic_LocalModuleConfig mc = meshtastic_LocalModuleConfig_init_default;
    mc.telemetry.device_telemetry_enabled = true;
    mc.telemetry.environment_measurement_enabled = true;
    mc.telemetry.air_quality_enabled = true;
    mc.telemetry.power_measurement_enabled = true;
    mc.telemetry.health_measurement_enabled = true;
    mc.mqtt.map_reporting_enabled = true;
    mc.mqtt.map_report_settings.should_report_location = true;

    optInDisableTelemetryBroadcast(mc);

    TEST_ASSERT_FALSE(mc.telemetry.device_telemetry_enabled);
    TEST_ASSERT_FALSE(mc.telemetry.environment_measurement_enabled);
    TEST_ASSERT_FALSE(mc.telemetry.air_quality_enabled);
    TEST_ASSERT_FALSE(mc.telemetry.power_measurement_enabled);
    TEST_ASSERT_FALSE(mc.telemetry.health_measurement_enabled);
    TEST_ASSERT_FALSE(mc.mqtt.map_report_settings.should_report_location);
    // Anonymous map presence (uplink) is intentionally left untouched.
    TEST_ASSERT_TRUE(mc.mqtt.map_reporting_enabled);
}

} // namespace

void setUp(void) {}

void tearDown(void) {}

extern "C" {
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_publicKey_openPrimaryIsPublic);
    RUN_TEST(test_publicKey_singleByteIsPublic);
    RUN_TEST(test_publicKey_defaultpskFamilyIsPublic);
    RUN_TEST(test_publicKey_private16IsPrivate);
    RUN_TEST(test_publicKey_private32IsPrivate);
    RUN_TEST(test_publicKey_disabledIsNotPublic);
    RUN_TEST(test_publicKey_secondaryInheritsPrivatePrimary);
    RUN_TEST(test_publicKey_secondaryInheritsPublicPrimary);
    RUN_TEST(test_optIn_zeroesPublicPreservesPrivate);
    RUN_TEST(test_optIn_zeroesOpenChannel);
    RUN_TEST(test_optIn_secondaryUnderPrivatePreserved);
    RUN_TEST(test_optIn_secondaryUnderPublicZeroed);
    RUN_TEST(test_optIn_setsHasModuleSettingsOnZeroed);
    RUN_TEST(test_optIn_telemetryAllFlagsOff);
    exit(UNITY_END());
}

void loop() {}
}
