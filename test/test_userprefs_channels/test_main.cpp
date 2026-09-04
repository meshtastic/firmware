// Channels::initDefaults() must populate every index named by USERPREFS_CHANNELS_TO_WRITE, and an
// index configured in part must keep the firmware's own value for every field left out.
//
// Under coverage-channel-table / native-windows-channel-table userprefs_fixture.h is -include'd and
// the configured cases run; under any other env the suite asserts the stock defaults instead.

#include "Channels.h"
#include "MeshTypes.h" // Include BEFORE TestUtil.h (provides NodeNum, isBroadcast, etc.)
#include "NodeDB.h"    // channelFile
#include "TestUtil.h"
#include "mesh-pb-constants.h"
#include <cstdio>
#include <cstring>
#include <unity.h>

#if defined(ARCH_PORTDUINO)
#define UPC_TEST_ENTRY extern "C"
#else
#define UPC_TEST_ENTRY
#endif

// The well-known default PSK in its 1-byte short form, i.e. what initDefaultChannel() leaves on an
// index no userPref touched.
static const uint8_t kDefaultPskShortForm = 0x01;

void setUp(void)
{
    memset(&channelFile, 0, sizeof(channelFile));
    channels.initDefaults();
}

void tearDown(void) {}

static void expectShortFormDefaultPsk(const meshtastic_ChannelSettings &s)
{
    TEST_ASSERT_EQUAL_UINT(1, s.psk.size);
    TEST_ASSERT_EQUAL_UINT8(kDefaultPskShortForm, s.psk.bytes[0]);
}

// An index initDefaultChannel() wrote but no userPref configured: default PSK, empty name, position
// sharing off, nothing muted, no MQTT.
static void expectStockChannel(uint8_t idx)
{
    const meshtastic_Channel &ch = channels.getByIndex(idx);
    TEST_ASSERT_TRUE(ch.has_settings);
    TEST_ASSERT_TRUE(ch.settings.has_module_settings);
    expectShortFormDefaultPsk(ch.settings);
    TEST_ASSERT_EQUAL_STRING("", ch.settings.name);
    TEST_ASSERT_EQUAL_UINT(0, ch.settings.module_settings.position_precision);
    TEST_ASSERT_FALSE(ch.settings.module_settings.is_muted);
    TEST_ASSERT_FALSE(ch.settings.uplink_enabled);
    TEST_ASSERT_FALSE(ch.settings.downlink_enabled);
}

#ifdef USERPREFS_CHANNELS_TO_WRITE

static const uint8_t kChannel0Psk[] = USERPREFS_CHANNEL_0_PSK;
static const uint8_t kChannel3Psk[] = USERPREFS_CHANNEL_3_PSK;

// The bug this table replaces: USERPREFS_CHANNELS_TO_WRITE past 3 produced live secondary channels
// carrying the public default PSK instead of the vendor's, because initDefaultChannel()'s switch
// stopped at case 2.
static void test_index_beyond_two_gets_its_configured_psk()
{
    const meshtastic_ChannelSettings &s = channels.getByIndex(3).settings;
    TEST_ASSERT_EQUAL_UINT(sizeof(kChannel3Psk), s.psk.size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kChannel3Psk, s.psk.bytes, sizeof(kChannel3Psk));
    TEST_ASSERT_EQUAL_STRING(USERPREFS_CHANNEL_3_NAME, s.name);
}

static void test_index_zero_matches_its_userprefs()
{
    const meshtastic_ChannelSettings &s = channels.getByIndex(0).settings;
    TEST_ASSERT_EQUAL_UINT(sizeof(kChannel0Psk), s.psk.size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kChannel0Psk, s.psk.bytes, sizeof(kChannel0Psk));
    TEST_ASSERT_EQUAL_STRING(USERPREFS_CHANNEL_0_NAME, s.name);
    TEST_ASSERT_EQUAL_UINT(USERPREFS_CHANNEL_0_PRECISION, s.module_settings.position_precision);
    TEST_ASSERT_TRUE(s.module_settings.is_muted);
    TEST_ASSERT_TRUE(s.uplink_enabled);
    TEST_ASSERT_FALSE(s.downlink_enabled);
}

static void test_every_written_index_has_a_role()
{
    for (int i = 0; i < USERPREFS_CHANNELS_TO_WRITE; i++) {
        const meshtastic_Channel &ch = channels.getByIndex(i);
        TEST_ASSERT_TRUE(ch.has_settings);
        TEST_ASSERT_TRUE(ch.settings.has_module_settings);
        TEST_ASSERT_EQUAL(i == 0 ? meshtastic_Channel_Role_PRIMARY : meshtastic_Channel_Role_SECONDARY, ch.role);
    }
}

// Index 1 is inside CHANNELS_TO_WRITE but has no userPref of its own.
static void test_unconfigured_index_inside_range_is_stock()
{
    expectStockChannel(1);
}

// Index 4 sets only a name upstream; the generator fills the rest with the values
// initDefaultChannel() would otherwise have left in place, so a partial config is not a way to
// accidentally ship uplink or a raised position precision.
static void test_partly_configured_index_keeps_firmware_defaults()
{
    const meshtastic_ChannelSettings &s = channels.getByIndex(4).settings;
    TEST_ASSERT_EQUAL_STRING(USERPREFS_CHANNEL_4_NAME, s.name);
    expectShortFormDefaultPsk(s);
    TEST_ASSERT_EQUAL_UINT(0, s.module_settings.position_precision);
    TEST_ASSERT_FALSE(s.module_settings.is_muted);
    TEST_ASSERT_FALSE(s.uplink_enabled);
    TEST_ASSERT_FALSE(s.downlink_enabled);
}

// Indices past USERPREFS_CHANNELS_TO_WRITE are never handed to initDefaultChannel(), so they must
// stay zeroed rather than picking up a neighbour's key.
static void test_index_past_channels_to_write_is_untouched()
{
    for (int i = USERPREFS_CHANNELS_TO_WRITE; i < MAX_NUM_CHANNELS; i++) {
        const meshtastic_ChannelSettings &s = channels.getByIndex(i).settings;
        TEST_ASSERT_EQUAL_STRING("", s.name);
        TEST_ASSERT_EQUAL_UINT(0, s.psk.size);
    }
}

// The switch this table replaces used strcpy() into a char[12].
static void test_over_long_name_is_truncated_and_terminated()
{
    const meshtastic_ChannelSettings &s = channels.getByIndex(2).settings;
    TEST_ASSERT_TRUE(strlen(USERPREFS_CHANNEL_2_NAME) >= sizeof(s.name));
    TEST_ASSERT_EQUAL_UINT(sizeof(s.name) - 1, strlen(s.name));
    TEST_ASSERT_EQUAL_CHAR('\0', s.name[sizeof(s.name) - 1]);
    TEST_ASSERT_EQUAL_MEMORY(USERPREFS_CHANNEL_2_NAME, s.name, sizeof(s.name) - 1);
}

#else // no channel userPrefs: the baseline the table must not have moved

static void test_stock_build_writes_only_index_zero()
{
    expectStockChannel(0);
    TEST_ASSERT_EQUAL(meshtastic_Channel_Role_PRIMARY, channels.getByIndex(0).role);
    for (int i = 1; i < MAX_NUM_CHANNELS; i++) {
        const meshtastic_ChannelSettings &s = channels.getByIndex(i).settings;
        TEST_ASSERT_EQUAL_STRING("", s.name);
        TEST_ASSERT_EQUAL_UINT(0, s.psk.size);
    }
}

static void test_stock_build_fills_the_whole_table()
{
    TEST_ASSERT_EQUAL_UINT(MAX_NUM_CHANNELS, channelFile.channels_count);
}

#endif

UPC_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();

#ifdef USERPREFS_CHANNELS_TO_WRITE
    printf("\n=== channel table beyond index 2 ===\n");
    RUN_TEST(test_index_beyond_two_gets_its_configured_psk);
    RUN_TEST(test_index_zero_matches_its_userprefs);
    RUN_TEST(test_every_written_index_has_a_role);

    printf("\n=== omitted fields and bounds ===\n");
    RUN_TEST(test_unconfigured_index_inside_range_is_stock);
    RUN_TEST(test_partly_configured_index_keeps_firmware_defaults);
    RUN_TEST(test_index_past_channels_to_write_is_untouched);
    RUN_TEST(test_over_long_name_is_truncated_and_terminated);
#else
    printf("\n=== stock defaults (no channel userPrefs) ===\n");
    RUN_TEST(test_stock_build_writes_only_index_zero);
    RUN_TEST(test_stock_build_fills_the_whole_table);
#endif

    exit(UNITY_END());
}

UPC_TEST_ENTRY void loop() {}
