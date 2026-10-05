// A vendor build writes USERPREFS_MESH_BEACON_* straight into moduleConfig, so the beacon config
// it ships never passes through AdminModule. NodeDB::resetRadioConfig() is the only gate it meets,
// and this suite is the only place the compile-time block is even compiled.
//
// Under coverage-beacon-userprefs / native-windows-beacon-userprefs userprefs_fixture.h is
// -include'd and the configured cases run; under any other env the suite asserts the stock default.

#include "FSCommon.h"
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

#ifdef USERPREFS_MESH_BEACON_OFFER_CHANNEL_NAME

static const uint8_t kOfferPsk[] = USERPREFS_MESH_BEACON_OFFER_CHANNEL_PSK;
// What the fixture strings must become: each cut before the 3-byte character across its limit, never inside it.
static const char kExpectedOfferName[] = "OfferedCou";
static const char kExpectedMessage[] = "Join the county mesh - 012345678901234567890123456789012345";

// ChannelSettings.name is a char[12] and the fixture name is longer, with a multi-byte character across
// the 11-byte limit. A byte cut there leaves invalid UTF-8, which fails nanopb's encode on every save.
static void test_over_long_offer_channel_name_is_cut_on_a_character_boundary()
{
    const auto &name = moduleConfig.mesh_beacon.broadcast_offer_channel.name;
    TEST_ASSERT_TRUE(strlen(USERPREFS_MESH_BEACON_OFFER_CHANNEL_NAME) >= sizeof(name));
    TEST_ASSERT_EQUAL_STRING_MESSAGE(kExpectedOfferName, name, "cut before the character that straddles the limit");
}

// Region carries no has_ flag - UNSET is the absence - so an in-process writer that sets only the
// value has to survive to the config. This is the case that broke when region was `optional`.
static void test_target_region_preset_and_slot_are_applied()
{
    const auto &b = moduleConfig.mesh_beacon;
    TEST_ASSERT_EQUAL_UINT(1, b.broadcast_targets_count);
    const auto &t = b.broadcast_targets[0];
    TEST_ASSERT_EQUAL(USERPREFS_MESH_BEACON_TARGET_0_REGION, t.region);
    TEST_ASSERT_TRUE(t.has_preset);
    TEST_ASSERT_EQUAL(USERPREFS_MESH_BEACON_TARGET_0_PRESET, t.preset);
    TEST_ASSERT_TRUE(t.has_frequency_slot);
    TEST_ASSERT_EQUAL_UINT32(USERPREFS_MESH_BEACON_TARGET_0_FREQUENCY_SLOT, t.frequency_slot);
    TEST_ASSERT_FALSE_MESSAGE(t.has_channel_index, "no index named, so it transmits on the primary");
}

static void test_offer_channel_is_applied()
{
    const auto &b = moduleConfig.mesh_beacon;
    TEST_ASSERT_TRUE(b.has_broadcast_offer_channel);
    TEST_ASSERT_EQUAL_STRING(kExpectedOfferName, b.broadcast_offer_channel.name);
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
    // Cut before the multi-byte character across the 60-byte limit, so the config still encodes.
    TEST_ASSERT_EQUAL_STRING(kExpectedMessage, b.broadcast_message);
}

// A vendor build never passes through AdminModule, so this is the only place the floor is applied.
static void test_below_floor_interval_is_clamped()
{
    TEST_ASSERT_TRUE(USERPREFS_MESH_BEACON_INTERVAL_SECS < default_mesh_beacon_min_broadcast_interval_secs);
    TEST_ASSERT_EQUAL_UINT32(default_mesh_beacon_min_broadcast_interval_secs, moduleConfig.mesh_beacon.broadcast_interval_secs);
}

// The fixture's target and offer are valid, so the boot gate must leave both standing.
static void test_by_value_target_is_not_cleared_by_the_boot_gate()
{
    TEST_ASSERT_TRUE(moduleConfig.mesh_beacon.has_broadcast_offer_channel);
    TEST_ASSERT_EQUAL_UINT(1, moduleConfig.mesh_beacon.broadcast_targets_count);

    // Idempotent: running the gate again must not erode a config it already accepted.
    MeshBeaconModule::sanitiseConfig(moduleConfig.mesh_beacon);
    TEST_ASSERT_TRUE(moduleConfig.mesh_beacon.has_broadcast_offer_channel);
    TEST_ASSERT_EQUAL(USERPREFS_MESH_BEACON_TARGET_0_REGION, moduleConfig.mesh_beacon.broadcast_targets[0].region);
}

// A vendor build never passes through AdminModule either for placement: the boot gate is where the
// offered channel enters the table, so the node holds what it advertises from the first boot.
static void test_offer_channel_is_placed_in_the_table_at_boot()
{
    const auto &ch = moduleConfig.mesh_beacon.broadcast_offer_channel;
    const int16_t idx = channels.findByIdentity(ch.name, ch.psk.bytes, (uint8_t)ch.psk.size, ch.use_aead);
    TEST_ASSERT_GREATER_THAN_INT16_MESSAGE(0, idx, "placed as a secondary, never in the primary slot");
    TEST_ASSERT_EQUAL(meshtastic_Channel_Role_SECONDARY, channels.getByIndex(idx).role);
}

// A vendor shipping the offer by value must still be administrable from a phone over LoRa.
static void test_shipped_config_fits_a_remote_admin_read_back()
{
    TEST_ASSERT_TRUE(MeshBeaconModule::fitsRemoteAdmin(moduleConfig.mesh_beacon));
}

// The offer channel's slot, by the identity the offer names; -1 when no live slot holds it.
static int16_t offerChannelSlot()
{
    const auto &ch = moduleConfig.mesh_beacon.broadcast_offer_channel;
    return channels.findByIdentity(ch.name, ch.psk.bytes, (uint8_t)ch.psk.size, ch.use_aead);
}

/*
 * Under test: NodeDB::resetRadioConfig() placing the offer channel only when it rebuilds the channel table
 * (src/mesh/NodeDB.cpp, beaconChannelsFromDefaults).
 * Why: an operator who deletes the offer channel has given the newer instruction. A table loaded intact from
 * channels.proto is not a fresh one, so an ordinary reboot must leave the deleted channel deleted.
 * Regression guarded: the placement firing on every boot, so a deleted offer channel came back each restart.
 */
static void test_deleted_offer_channel_stays_deleted_across_a_reboot()
{
    const int16_t idx = offerChannelSlot();
    TEST_ASSERT_GREATER_THAN_INT16_MESSAGE(0, idx, "precondition: placed at boot");
    meshtastic_Channel gone = channels.getByIndex(idx);
    gone.role = meshtastic_Channel_Role_DISABLED;
    channels.setChannel(gone);
    channels.onConfigChanged();
    TEST_ASSERT_TRUE(nodeDB->saveToDisk(SEGMENT_CHANNELS | SEGMENT_MODULECONFIG));

    delete nodeDB; // an ordinary reboot: both files load intact
    nodeDB = new NodeDB();

    TEST_ASSERT_TRUE_MESSAGE(moduleConfig.mesh_beacon.has_broadcast_offer_channel, "the offer itself is kept");
    TEST_ASSERT_LESS_THAN_INT16_MESSAGE(0, offerChannelSlot(), "the deleted channel is not placed again");
    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_Channel_Role_DISABLED, channels.getByIndex(idx).role, "its slot stays disabled");
}

/*
 * Under test: the same placement when channels.proto is lost but module.proto, with the offer, loads.
 * Why: the table is rebuilt from defaults, so the offer's channel belongs in it again - the config still asks for
 * it, and nothing the operator did removed it from this table.
 * Regression guarded: a lost channel file leaving the offer withheld for good. Until now only an admin_radio case
 * leaking its offer onto disk exercised this path, and by accident.
 */
static void test_lost_channel_file_places_the_offer_channel_again()
{
    TEST_ASSERT_TRUE(moduleConfig.mesh_beacon.has_broadcast_offer_channel);
    FSCom.remove(channelFileName);

    delete nodeDB; // the boot after the channel file went missing
    nodeDB = new NodeDB();

    TEST_ASSERT_GREATER_THAN_INT16_MESSAGE(0, offerChannelSlot(), "a rebuilt table places the offer's channel");
}

/*
 * Under test: NodeDB::resetRadioConfig() setting beaconChannelsFromDefaults whenever it rebuilds the
 * channel table from defaults, and placeDefaultBeaconChannels() consuming it (src/mesh/NodeDB.cpp).
 *
 * Why: a factory reset saves an empty channel file and the module config with the offer. The boot after
 * it loads that module config cleanly, so only the channel-table rebuild can say "this table is fresh,
 * the vendor's offer channel belongs in it". An operator-deleted channel is only disabled, so a table
 * that is not rebuilt never brings one back.
 *
 * Regression guarded: the offer channel missing from the table after a factory reset, so the beacon
 * withholds its offer from then on - a vendor build that stops inviting anyone after a reset.
 * Runs last: it replaces the NodeDB the other cases read.
 */
static void test_offer_channel_survives_a_factory_reset()
{
    nodeDB->factoryReset(false);
    delete nodeDB; // the reset reboots; a fresh NodeDB loads exactly what it saved
    nodeDB = new NodeDB();

    const auto &ch = moduleConfig.mesh_beacon.broadcast_offer_channel;
    const int16_t idx = channels.findByIdentity(ch.name, ch.psk.bytes, (uint8_t)ch.psk.size, ch.use_aead);
    TEST_ASSERT_GREATER_THAN_INT16_MESSAGE(0, idx, "the offer channel must be in the table after a factory reset");
}

#else // no beacon userPrefs: the baseline the block must not have moved

static void test_stock_build_ships_no_beacon_target()
{
    const auto &b = moduleConfig.mesh_beacon;
    TEST_ASSERT_FALSE(b.has_broadcast_offer_channel);
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

#ifdef USERPREFS_MESH_BEACON_OFFER_CHANNEL_NAME
    printf("\n=== by-value beacon userPrefs ===\n");
    RUN_TEST(test_over_long_offer_channel_name_is_cut_on_a_character_boundary);
    RUN_TEST(test_target_region_preset_and_slot_are_applied);
    RUN_TEST(test_offer_channel_is_applied);
    RUN_TEST(test_flags_and_message_are_applied);

    printf("\n=== the boot gate ===\n");
    RUN_TEST(test_below_floor_interval_is_clamped);
    RUN_TEST(test_by_value_target_is_not_cleared_by_the_boot_gate);
    RUN_TEST(test_offer_channel_is_placed_in_the_table_at_boot);
    RUN_TEST(test_shipped_config_fits_a_remote_admin_read_back);

    printf("\n=== reboots ===\n");
    RUN_TEST(test_deleted_offer_channel_stays_deleted_across_a_reboot);
    RUN_TEST(test_lost_channel_file_places_the_offer_channel_again);

    printf("\n=== factory reset ===\n");
    RUN_TEST(test_offer_channel_survives_a_factory_reset);
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
