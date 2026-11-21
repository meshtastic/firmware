// Unit tests for SensitiveFields (src/mesh/SensitiveFields.cpp): isSensitive, redact, anySet, restore,
// restoreSection and descriptorFor, driven by the generated registry
// src/mesh/generated/meshtastic/field_metadata_registry.h.
//
// Why this behavior is required: licensed (ham) mode carries admin unencrypted - no PKC - so
// AdminModule blanks every field the protobufs mark sensitive in a reply to a licensed remote,
// refuses a licensed remote setter that carries one, and MeshService refuses to send a client's
// licensed admin packet that carries one. All three ask this walker. It must find a sensitive field
// wherever an AdminMessage nests it (inside a oneof, an optional submessage, a repeated entry), must
// not read the inactive members of a oneof (their bytes alias the active one), and must let a client's
// read-modify-write round trip through a redacted response keep the stored secret rather than erase it.
//
// Regressions guarded if these assertions are deleted or relaxed:
//   - a registry entry whose message type the firmware cannot map to a nanopb descriptor. nanopb
//     descriptors carry no type name, so SensitiveFields keeps its own name table; a sensitive field
//     added to the protobufs in a type missing from that table would cross the air in the clear with
//     nothing failing but this suite.
//   - a walker that stops at the top level, or skips optional/repeated submessages, so a nested secret
//     (a channel's psk, the Bluetooth pin inside DeviceConnectionStatus) is transmitted.
//   - a walker that inspects every member of a oneof: union bytes left by another member would read
//     as a set wifi_psk and refuse an innocent LoRa setter.
//   - restore overwriting a value the sender did set, or failing to fill a blank one - the first lets a
//     caller bypass the refusal it guards, the second erases the stored secret on every licensed edit.

#include "Arduino.h"
#include "TestUtil.h"
#include <unity.h>

#include "mesh/SensitiveFields.h"
#include "mesh/generated/meshtastic/admin.pb.h"
#include "mesh/generated/meshtastic/deviceonly.pb.h"
#include "mesh/generated/meshtastic/field_metadata_registry.h"
#include "mesh/generated/meshtastic/localonly.pb.h"
#include <cstring>

static meshtastic_AdminMessage networkResponse(const char *ssid, const char *psk)
{
    meshtastic_AdminMessage am = meshtastic_AdminMessage_init_zero;
    am.which_payload_variant = meshtastic_AdminMessage_get_config_response_tag;
    am.get_config_response.which_payload_variant = meshtastic_Config_network_tag;
    strncpy(am.get_config_response.payload_variant.network.wifi_ssid, ssid,
            sizeof(am.get_config_response.payload_variant.network.wifi_ssid) - 1);
    strncpy(am.get_config_response.payload_variant.network.wifi_psk, psk,
            sizeof(am.get_config_response.payload_variant.network.wifi_psk) - 1);
    return am;
}

static void setPsk(meshtastic_ChannelSettings &s, uint8_t fill, pb_size_t size)
{
    s.psk.size = size;
    memset(s.psk.bytes, fill, size);
}

void setUp(void) {}
void tearDown(void) {}

// The name table must cover the registry. Without it, a new sensitive field silently goes unprotected.
void test_every_registry_entry_resolves_to_a_descriptor(void)
{
    for (size_t i = 0; i < MESHTASTIC_FIELD_METADATA_COUNT; i++) {
        const meshtastic_field_metadata_t &m = MESHTASTIC_FIELD_METADATA[i];
        const pb_msgdesc_t *desc = SensitiveFields::descriptorFor(m.message_type);
        TEST_ASSERT_NOT_NULL_MESSAGE(desc, m.message_type);
        TEST_ASSERT_TRUE_MESSAGE(SensitiveFields::isSensitive(desc, (pb_size_t)m.field_tag), m.message_type);
    }
}

void test_isSensitive_is_false_for_unlisted_fields(void)
{
    TEST_ASSERT_TRUE(SensitiveFields::isSensitive(&meshtastic_Config_NetworkConfig_msg, 4));  // wifi_psk
    TEST_ASSERT_FALSE(SensitiveFields::isSensitive(&meshtastic_Config_NetworkConfig_msg, 3)); // wifi_ssid
    // Same tag as a sensitive field, but in a message type the registry does not name.
    TEST_ASSERT_FALSE(SensitiveFields::isSensitive(&meshtastic_Config_LoRaConfig_msg, 4));
    TEST_ASSERT_NULL(SensitiveFields::descriptorFor("meshtastic.Config.LoRaConfig"));
}

void test_redact_blanks_the_sensitive_field_and_keeps_the_rest(void)
{
    meshtastic_AdminMessage am = networkResponse("HomeNet", "hunter2hunter2");

    TEST_ASSERT_TRUE(SensitiveFields::redact(&meshtastic_AdminMessage_msg, &am));
    TEST_ASSERT_EQUAL_STRING("", am.get_config_response.payload_variant.network.wifi_psk);
    TEST_ASSERT_EQUAL_STRING("HomeNet", am.get_config_response.payload_variant.network.wifi_ssid);
}

// AdminModule re-encodes a reply only when redact reports something was blanked.
void test_redact_reports_nothing_when_already_blank(void)
{
    meshtastic_AdminMessage am = networkResponse("HomeNet", "");
    TEST_ASSERT_FALSE(SensitiveFields::redact(&meshtastic_AdminMessage_msg, &am));
}

// ChannelSettings is an optional submessage of Channel; the Bluetooth pin sits in an optional
// submessage of DeviceConnectionStatus.
void test_redact_reaches_optional_submessages(void)
{
    meshtastic_AdminMessage ch = meshtastic_AdminMessage_init_zero;
    ch.which_payload_variant = meshtastic_AdminMessage_get_channel_response_tag;
    ch.get_channel_response.has_settings = true;
    strcpy(ch.get_channel_response.settings.name, "Club");
    setPsk(ch.get_channel_response.settings, 0x5A, 16);
    TEST_ASSERT_TRUE(SensitiveFields::redact(&meshtastic_AdminMessage_msg, &ch));
    TEST_ASSERT_EQUAL(0, ch.get_channel_response.settings.psk.size);
    TEST_ASSERT_EQUAL_STRING("Club", ch.get_channel_response.settings.name);

    meshtastic_AdminMessage conn = meshtastic_AdminMessage_init_zero;
    conn.which_payload_variant = meshtastic_AdminMessage_get_device_connection_status_response_tag;
    conn.get_device_connection_status_response.has_bluetooth = true;
    conn.get_device_connection_status_response.bluetooth.pin = 123456;
    conn.get_device_connection_status_response.bluetooth.is_connected = true;
    TEST_ASSERT_TRUE(SensitiveFields::redact(&meshtastic_AdminMessage_msg, &conn));
    TEST_ASSERT_EQUAL_UINT32(0, conn.get_device_connection_status_response.bluetooth.pin);
    TEST_ASSERT_TRUE(conn.get_device_connection_status_response.bluetooth.is_connected);
}

// Each used entry of a repeated submessage is walked; entries past the count are not.
void test_redact_walks_used_repeated_entries_only(void)
{
    meshtastic_ChannelFile file = meshtastic_ChannelFile_init_zero;
    file.channels_count = 2;
    for (auto &c : file.channels) {
        c.has_settings = true;
        setPsk(c.settings, 0x11, 16);
    }

    TEST_ASSERT_TRUE(SensitiveFields::redact(&meshtastic_ChannelFile_msg, &file));
    TEST_ASSERT_EQUAL(0, file.channels[0].settings.psk.size);
    TEST_ASSERT_EQUAL(0, file.channels[1].settings.psk.size);
    TEST_ASSERT_EQUAL(16, file.channels[2].settings.psk.size);
}

// The union bytes of an inactive oneof member alias the active one, so they must not be read.
void test_anySet_ignores_inactive_oneof_members(void)
{
    meshtastic_AdminMessage am = meshtastic_AdminMessage_init_zero;
    am.which_payload_variant = meshtastic_AdminMessage_set_config_tag;
    am.set_config.which_payload_variant = meshtastic_Config_lora_tag;
    memset(&am.set_config.payload_variant, 0x7F, sizeof(am.set_config.payload_variant));
    TEST_ASSERT_FALSE(SensitiveFields::anySet(&meshtastic_AdminMessage_msg, &am));

    am.set_config.which_payload_variant = meshtastic_Config_network_tag;
    TEST_ASSERT_TRUE(SensitiveFields::anySet(&meshtastic_AdminMessage_msg, &am));
}

// A redacted pin reads back as 0, so 0 has to count as blank for the round trip to be accepted.
void test_anySet_treats_a_zero_scalar_as_blank(void)
{
    meshtastic_AdminMessage am = meshtastic_AdminMessage_init_zero;
    am.which_payload_variant = meshtastic_AdminMessage_set_config_tag;
    am.set_config.which_payload_variant = meshtastic_Config_bluetooth_tag;
    am.set_config.payload_variant.bluetooth.enabled = true;
    TEST_ASSERT_FALSE(SensitiveFields::anySet(&meshtastic_AdminMessage_msg, &am));

    am.set_config.payload_variant.bluetooth.fixed_pin = 123456;
    TEST_ASSERT_TRUE(SensitiveFields::anySet(&meshtastic_AdminMessage_msg, &am));
}

// A set_config carries one section of LocalConfig; a blank secret in it takes the stored value.
void test_restoreSection_fills_a_blank_secret_from_the_store(void)
{
    meshtastic_LocalConfig store = meshtastic_LocalConfig_init_zero;
    strcpy(store.network.wifi_ssid, "OldNet");
    strcpy(store.network.wifi_psk, "hunter2hunter2");

    meshtastic_Config incoming = meshtastic_Config_init_zero;
    incoming.which_payload_variant = meshtastic_Config_network_tag;
    strcpy(incoming.payload_variant.network.wifi_ssid, "NewNet");

    SensitiveFields::restoreSection(&meshtastic_Config_msg, &incoming, &meshtastic_LocalConfig_msg, &store);
    TEST_ASSERT_EQUAL_STRING("hunter2hunter2", incoming.payload_variant.network.wifi_psk);
    TEST_ASSERT_EQUAL_STRING("NewNet", incoming.payload_variant.network.wifi_ssid);
}

// restore only fills blanks. Overwriting a set value would turn a refused change into a silent no-op
// for any caller that restores before checking.
void test_restore_leaves_a_set_value_alone(void)
{
    meshtastic_Config_NetworkConfig stored = meshtastic_Config_NetworkConfig_init_zero;
    strcpy(stored.wifi_psk, "hunter2hunter2");
    meshtastic_Config_NetworkConfig incoming = meshtastic_Config_NetworkConfig_init_zero;
    strcpy(incoming.wifi_psk, "chosen");

    SensitiveFields::restore(&meshtastic_Config_NetworkConfig_msg, &incoming, &stored);
    TEST_ASSERT_EQUAL_STRING("chosen", incoming.wifi_psk);
}

// Bytes fields restore with their size; the psk sits in Channel's optional settings submessage.
void test_restore_fills_a_blank_channel_psk(void)
{
    meshtastic_Channel stored = meshtastic_Channel_init_zero;
    stored.has_settings = true;
    setPsk(stored.settings, 0xC3, 32);
    meshtastic_Channel incoming = meshtastic_Channel_init_zero;
    incoming.has_settings = true;
    strcpy(incoming.settings.name, "Renamed");

    SensitiveFields::restore(&meshtastic_Channel_msg, &incoming, &stored);
    TEST_ASSERT_EQUAL(32, incoming.settings.psk.size);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xC3, incoming.settings.psk.bytes, 32);
    TEST_ASSERT_EQUAL_STRING("Renamed", incoming.settings.name);
}

// Config.device_ui has no LocalConfig counterpart; nothing to restore from, and nothing is touched.
void test_restoreSection_without_a_stored_section_changes_nothing(void)
{
    meshtastic_LocalConfig store = meshtastic_LocalConfig_init_zero;
    meshtastic_Config incoming = meshtastic_Config_init_zero;
    incoming.which_payload_variant = meshtastic_Config_device_ui_tag;
    incoming.payload_variant.device_ui.screen_brightness = 42;

    SensitiveFields::restoreSection(&meshtastic_Config_msg, &incoming, &meshtastic_LocalConfig_msg, &store);
    TEST_ASSERT_EQUAL(meshtastic_Config_device_ui_tag, incoming.which_payload_variant);
    TEST_ASSERT_EQUAL_UINT32(42, incoming.payload_variant.device_ui.screen_brightness);
    TEST_ASSERT_EQUAL_UINT32(0, incoming.payload_variant.device_ui.pin_code);
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_every_registry_entry_resolves_to_a_descriptor);
    RUN_TEST(test_isSensitive_is_false_for_unlisted_fields);
    RUN_TEST(test_redact_blanks_the_sensitive_field_and_keeps_the_rest);
    RUN_TEST(test_redact_reports_nothing_when_already_blank);
    RUN_TEST(test_redact_reaches_optional_submessages);
    RUN_TEST(test_redact_walks_used_repeated_entries_only);
    RUN_TEST(test_anySet_ignores_inactive_oneof_members);
    RUN_TEST(test_anySet_treats_a_zero_scalar_as_blank);
    RUN_TEST(test_restoreSection_fills_a_blank_secret_from_the_store);
    RUN_TEST(test_restore_leaves_a_set_value_alone);
    RUN_TEST(test_restore_fills_a_blank_channel_psk);
    RUN_TEST(test_restoreSection_without_a_stored_section_changes_nothing);
    exit(UNITY_END());
}

void loop() {}
