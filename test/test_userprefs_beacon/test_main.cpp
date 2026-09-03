// A vendor build writes USERPREFS_MESH_BEACON_* straight into moduleConfig, so the beacon config
// it ships never passes through AdminModule. NodeDB::resetRadioConfig() is the only gate it meets,
// and this suite is the only place the compile-time block is even compiled.
//
// Under coverage-beacon-userprefs / native-windows-beacon-userprefs userprefs_fixture.h is
// -include'd and the configured cases run; under any other env the suite asserts the stock default.

#include "MeshTypes.h" // Include BEFORE TestUtil.h (provides NodeNum, isBroadcast, etc.)
#include "NodeDB.h"
#include "TestUtil.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unity.h>

#if defined(ARCH_PORTDUINO)
#define UPB_TEST_ENTRY extern "C"
#else
#define UPB_TEST_ENTRY
#endif

#if !MESHTASTIC_EXCLUDE_BEACON

#include "Default.h"
#include "modules/MeshBeaconModule.h"

void setUp(void) {}
void tearDown(void) {}

#ifdef USERPREFS_MESH_BEACON_ON_CHANNEL_NAME

static const uint8_t kOnPsk[] = USERPREFS_MESH_BEACON_ON_CHANNEL_PSK;
static const uint8_t kOfferPsk[] = USERPREFS_MESH_BEACON_OFFER_CHANNEL_PSK;

// The by-value target: name and PSK carried outright, so a vendor never has to describe a channel
// table index that may not hold what they expect.
static void test_on_channel_carries_its_own_name_and_psk()
{
    const auto &b = moduleConfig.mesh_beacon;
    TEST_ASSERT_TRUE(b.has_broadcast_on_channel);
    TEST_ASSERT_EQUAL_UINT(sizeof(kOnPsk), b.broadcast_on_channel.psk.size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kOnPsk, b.broadcast_on_channel.psk.bytes, sizeof(kOnPsk));
}

// ChannelIdentity.name is a char[12] and the fixture name is longer; the old ON_* block this
// replaces is where a strcpy would have run off the end.
static void test_over_long_on_channel_name_is_truncated_and_terminated()
{
    const auto &name = moduleConfig.mesh_beacon.broadcast_on_channel.name;
    TEST_ASSERT_TRUE(strlen(USERPREFS_MESH_BEACON_ON_CHANNEL_NAME) >= sizeof(name));
    TEST_ASSERT_EQUAL_UINT(sizeof(name) - 1, strlen(name));
    TEST_ASSERT_EQUAL_CHAR('\0', name[sizeof(name) - 1]);
    TEST_ASSERT_EQUAL_MEMORY(USERPREFS_MESH_BEACON_ON_CHANNEL_NAME, name, sizeof(name) - 1);
}

// Region carries no has_ flag - UNSET is the absence - so an in-process writer that sets only the
// value has to survive to the config. This is the case that broke when region was `optional`.
static void test_on_region_preset_and_slot_are_applied()
{
    const auto &b = moduleConfig.mesh_beacon;
    TEST_ASSERT_EQUAL(USERPREFS_MESH_BEACON_ON_REGION, b.broadcast_on_region);
    TEST_ASSERT_TRUE(b.has_broadcast_on_preset);
    TEST_ASSERT_EQUAL(USERPREFS_MESH_BEACON_ON_PRESET, b.broadcast_on_preset);
    TEST_ASSERT_TRUE(b.has_broadcast_on_frequency_slot);
    TEST_ASSERT_EQUAL_UINT32(USERPREFS_MESH_BEACON_ON_FREQUENCY_SLOT, b.broadcast_on_frequency_slot);
}

static void test_offer_channel_is_applied()
{
    const auto &b = moduleConfig.mesh_beacon;
    TEST_ASSERT_TRUE(b.has_broadcast_offer_channel);
    TEST_ASSERT_EQUAL_STRING(USERPREFS_MESH_BEACON_OFFER_CHANNEL_NAME, b.broadcast_offer_channel.name);
    TEST_ASSERT_EQUAL_UINT(sizeof(kOfferPsk), b.broadcast_offer_channel.psk.size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kOfferPsk, b.broadcast_offer_channel.psk.bytes, sizeof(kOfferPsk));
    TEST_ASSERT_EQUAL(USERPREFS_MESH_BEACON_OFFER_REGION, b.broadcast_offer_region);
    TEST_ASSERT_TRUE(b.has_broadcast_offer_frequency_slot);
    TEST_ASSERT_EQUAL_UINT32(USERPREFS_MESH_BEACON_OFFER_FREQUENCY_SLOT, b.broadcast_offer_frequency_slot);
}

static void test_flags_and_message_are_applied()
{
    const auto &b = moduleConfig.mesh_beacon;
    TEST_ASSERT_TRUE(b.flags & meshtastic_ModuleConfig_MeshBeaconConfig_Flags_FLAG_LISTEN_ENABLED);
    TEST_ASSERT_TRUE(b.flags & MESH_BEACON_FLAG_BROADCAST_ENABLED);
    TEST_ASSERT_EQUAL_STRING(USERPREFS_MESH_BEACON_MESSAGE, b.broadcast_message);
}

// A vendor build never passes through AdminModule, so this is the only place the floor is applied.
static void test_below_floor_interval_is_clamped()
{
    TEST_ASSERT_TRUE(USERPREFS_MESH_BEACON_INTERVAL_SECS < default_mesh_beacon_min_broadcast_interval_secs);
    TEST_ASSERT_EQUAL_UINT32(default_mesh_beacon_min_broadcast_interval_secs, moduleConfig.mesh_beacon.broadcast_interval_secs);
}

// The fixture names no indexed target, so the by-value half is the one that stands.
static void test_by_value_target_is_not_cleared_by_the_boot_gate()
{
    TEST_ASSERT_TRUE(MeshBeaconModule::hasExplicitTarget(moduleConfig.mesh_beacon));
    TEST_ASSERT_EQUAL_UINT(0, moduleConfig.mesh_beacon.broadcast_targets_count);

    // Idempotent: running the gate again must not erode a config it already accepted.
    MeshBeaconModule::sanitiseConfig(moduleConfig.mesh_beacon);
    TEST_ASSERT_TRUE(moduleConfig.mesh_beacon.has_broadcast_on_channel);
    TEST_ASSERT_EQUAL(USERPREFS_MESH_BEACON_ON_REGION, moduleConfig.mesh_beacon.broadcast_on_region);
}

// A vendor shipping both channels by value must still be administrable from a phone over LoRa.
static void test_shipped_config_fits_a_remote_admin_read_back()
{
    TEST_ASSERT_TRUE(MeshBeaconModule::fitsRemoteAdmin(moduleConfig.mesh_beacon));
}

#else // no beacon userPrefs: the baseline the block must not have moved

static void test_stock_build_ships_no_beacon_target()
{
    const auto &b = moduleConfig.mesh_beacon;
    TEST_ASSERT_FALSE(b.has_broadcast_on_channel);
    TEST_ASSERT_FALSE(b.has_broadcast_offer_channel);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_UNSET, b.broadcast_on_region);
    TEST_ASSERT_EQUAL_UINT(0, b.broadcast_targets_count);
}

#endif

UPB_TEST_ENTRY void setup()
{
    initializeTestEnvironment();

    // Cold-boot one NodeDB: its constructor is what runs installDefaultModuleConfig() (and with it
    // the compile-time block) followed by resetRadioConfig(), which is the beacon's only boot gate.
    nodeDB = new NodeDB();

    UNITY_BEGIN();

#ifdef USERPREFS_MESH_BEACON_ON_CHANNEL_NAME
    printf("\n=== by-value beacon userPrefs ===\n");
    RUN_TEST(test_on_channel_carries_its_own_name_and_psk);
    RUN_TEST(test_over_long_on_channel_name_is_truncated_and_terminated);
    RUN_TEST(test_on_region_preset_and_slot_are_applied);
    RUN_TEST(test_offer_channel_is_applied);
    RUN_TEST(test_flags_and_message_are_applied);

    printf("\n=== the boot gate ===\n");
    RUN_TEST(test_below_floor_interval_is_clamped);
    RUN_TEST(test_by_value_target_is_not_cleared_by_the_boot_gate);
    RUN_TEST(test_shipped_config_fits_a_remote_admin_read_back);
#else
    printf("\n=== stock defaults (no beacon userPrefs) ===\n");
    RUN_TEST(test_stock_build_ships_no_beacon_target);
#endif

    const int rc = UNITY_END();
    delete nodeDB;
    nodeDB = nullptr;
    exit(rc);
}

#else // MESHTASTIC_EXCLUDE_BEACON

void setUp(void) {}
void tearDown(void) {}

UPB_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}

#endif

UPB_TEST_ENTRY void loop() {}
