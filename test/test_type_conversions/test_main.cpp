// Tests for src/mesh/TypeConversions.cpp covering:
//   - bitfield bit collapse on store + extraction round-trip
//   - long_name / short_name truncation at the storage boundaries (wire User
//     stays 40 wide for decoding legacy senders; NodeInfoLite stores 25 / 5)
//   - wire-level decode acceptance of legacy 39-byte long_names
//   - public_key / hw_model / role pass-through
//   - thin vs bundled NodeInfo emission
//
// All exercised via the explicit-args overload of ConvertToNodeInfo so we don't
// touch the global nodeDB pointer (which isn't initialized in this test env).

#include "NodeDB.h"
#include "TestUtil.h"
#include "TypeConversions.h"
#include "mesh-pb-constants.h"
#include "meshUtils.h"
#include "modules/Telemetry/UnitConversions.h"
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// ---------- helpers -----------------------------------------------------------

static meshtastic_User makeUser(const char *longName, const char *shortName)
{
    meshtastic_User u = meshtastic_User_init_zero;
    if (longName)
        strncpy(u.long_name, longName, sizeof(u.long_name) - 1);
    if (shortName)
        strncpy(u.short_name, shortName, sizeof(u.short_name) - 1);
    u.hw_model = meshtastic_HardwareModel_TBEAM;
    u.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    return u;
}

// ---------- has_user / id / macaddr ------------------------------------------

void test_copy_user_sets_has_user_bit(void)
{
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    meshtastic_User u = makeUser("Kevin Hester", "kh");
    TEST_ASSERT_FALSE((lite.bitfield & NODEINFO_BITFIELD_HAS_USER_MASK) != 0);
    TypeConversions::CopyUserToNodeInfoLite(&lite, u);
    TEST_ASSERT_TRUE((lite.bitfield & NODEINFO_BITFIELD_HAS_USER_MASK) != 0);
}

void test_convert_to_user_id_derived_from_nodenum(void)
{
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    lite.num = 0x12345678;
    meshtastic_User u = TypeConversions::ConvertToUser(&lite);
    TEST_ASSERT_EQUAL_STRING("!12345678", u.id);
}

void test_convert_to_user_zero_fills_macaddr(void)
{
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    lite.num = 0xCAFEBABE;
    meshtastic_User u = TypeConversions::ConvertToUser(&lite);
    uint8_t zeros[sizeof(u.macaddr)] = {0};
    TEST_ASSERT_EQUAL_MEMORY(zeros, u.macaddr, sizeof(u.macaddr));
}

// ---------- long_name truncation ---------------------------------------------

void test_long_name_short_passes_through(void)
{
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    meshtastic_User u = makeUser("Kevin Hester", "kh"); // 12 chars, fits
    TypeConversions::CopyUserToNodeInfoLite(&lite, u);
    TEST_ASSERT_EQUAL_STRING("Kevin Hester", lite.long_name);
}

void test_long_name_exact_24_fits(void)
{
    // 24 chars -> stored as 24 chars + NUL inside char[25].
    const char *exact24 = "abcdefghijklmnopqrstuvwx";
    TEST_ASSERT_EQUAL_INT(24, (int)strlen(exact24));
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    meshtastic_User u = makeUser(exact24, "ex");
    TypeConversions::CopyUserToNodeInfoLite(&lite, u);
    TEST_ASSERT_EQUAL_STRING(exact24, lite.long_name);
    TEST_ASSERT_EQUAL_INT(24, (int)strlen(lite.long_name));
}

void test_long_name_truncates_when_too_long(void)
{
    // 33 chars in, must fit in 24 + NUL.
    const char *tooLong = "North-County Search & Rescue Base";
    TEST_ASSERT_EQUAL_INT(33, (int)strlen(tooLong));
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    meshtastic_User u = makeUser(tooLong, "nc");
    TypeConversions::CopyUserToNodeInfoLite(&lite, u);
    TEST_ASSERT_EQUAL_INT(24, (int)strlen(lite.long_name));
    TEST_ASSERT_EQUAL_STRING_LEN(tooLong, lite.long_name, 24);
    TEST_ASSERT_EQUAL_INT('\0', lite.long_name[24]);
}

void test_long_name_round_trip_to_wire(void)
{
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    meshtastic_User in = makeUser("Mountain Repeater Site E", "mr"); // exactly 24
    TypeConversions::CopyUserToNodeInfoLite(&lite, in);
    meshtastic_User out = TypeConversions::ConvertToUser(&lite);
    TEST_ASSERT_EQUAL_STRING(in.long_name, out.long_name);
}

void test_long_name_truncated_utf8_boundary_sanitized(void)
{
    // Suffix the 24th byte with the start of a 4-byte emoji; truncation should
    // leave the dangling bytes for sanitizeUtf8 to replace with '?'.
    char input[40] = {0};
    memset(input, 'a', 22);              // 22 ASCII
    input[22] = static_cast<char>(0xF0); // emoji lead byte at position 22
    input[23] = static_cast<char>(0x9F); // continuation
    input[24] = static_cast<char>(0xA4); // continuation - past the cap
    input[25] = static_cast<char>(0x96); // continuation - past the cap
    input[26] = '\0';
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    meshtastic_User u = makeUser(input, "u8");
    TypeConversions::CopyUserToNodeInfoLite(&lite, u);
    // 24 bytes survive plus NUL. sanitizeUtf8 should have turned the dangling
    // multi-byte head into '?' since its continuation bytes were chopped.
    TEST_ASSERT_EQUAL_INT(24, (int)strlen(lite.long_name));
    for (int i = 0; i < 22; ++i)
        TEST_ASSERT_EQUAL_INT('a', lite.long_name[i]);
    // The 4-byte sequence got truncated mid-codepoint; sanitizeUtf8 replaces
    // any invalid lead/continuation byte with '?'.
    TEST_ASSERT_EQUAL_INT('?', lite.long_name[22]);
    TEST_ASSERT_EQUAL_INT('?', lite.long_name[23]);
}

// ---------- wire decode width (decode-liberal, store-narrow) ------------------

// Hand-built wire-format User payload: field 2 (long_name), wire type 2.
static size_t makeUserPayload(uint8_t *buf, size_t nameLen)
{
    size_t i = 0;
    buf[i++] = 0x12; // tag: field 2, length-delimited
    buf[i++] = (uint8_t)nameLen;
    for (size_t j = 0; j < nameLen; j++)
        buf[i++] = (uint8_t)('A' + (j % 26));
    return i;
}

void test_wire_decode_accepts_legacy_39_byte_long_name(void)
{
    // The longest name a sender built against the old max_size:40 can emit.
    // nanopb halts on string overflow rather than truncating, so this only
    // passes while the wire-facing meshtastic_User stays 40 wide.
    uint8_t buf[64];
    size_t len = makeUserPayload(buf, 39);
    meshtastic_User u = meshtastic_User_init_zero;
    TEST_ASSERT_TRUE(pb_decode_from_bytes(buf, len, &meshtastic_User_msg, &u));
    TEST_ASSERT_EQUAL_INT(39, (int)strlen(u.long_name));

    // ...and the store boundary clamps it to the local cap.
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    TypeConversions::CopyUserToNodeInfoLite(&lite, u);
    TEST_ASSERT_EQUAL_INT(MAX_LONG_NAME_BYTES, (int)strlen(lite.long_name));
}

void test_wire_decode_rejects_name_beyond_wire_limit(void)
{
    // 45 bytes exceeds even the 40-byte wire buffer; the whole message is
    // rejected (documents the hard outer bound).
    uint8_t buf[64];
    size_t len = makeUserPayload(buf, 45);
    meshtastic_User u = meshtastic_User_init_zero;
    TEST_ASSERT_FALSE(pb_decode_from_bytes(buf, len, &meshtastic_User_msg, &u));
}

// ---------- short_name truncation --------------------------------------------

void test_short_name_passes_through(void)
{
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    meshtastic_User u = makeUser("Test", "abcd"); // 4 chars, fits short_name[5]
    TypeConversions::CopyUserToNodeInfoLite(&lite, u);
    TEST_ASSERT_EQUAL_STRING("abcd", lite.short_name);
}

void test_short_name_truncates_when_too_long(void)
{
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    meshtastic_User u = makeUser("Test", "abcdefgh");
    TypeConversions::CopyUserToNodeInfoLite(&lite, u);
    TEST_ASSERT_EQUAL_INT(4, (int)strlen(lite.short_name));
    TEST_ASSERT_EQUAL_INT('\0', lite.short_name[4]);
}

// ---------- bitfield collapse + round-trip per bool --------------------------

void test_bitfield_is_licensed_round_trip(void)
{
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    meshtastic_User in = makeUser("a", "a");
    in.is_licensed = true;
    TypeConversions::CopyUserToNodeInfoLite(&lite, in);
    TEST_ASSERT_TRUE((lite.bitfield & NODEINFO_BITFIELD_IS_LICENSED_MASK) != 0);
    meshtastic_User out = TypeConversions::ConvertToUser(&lite);
    TEST_ASSERT_TRUE(out.is_licensed);

    in.is_licensed = false;
    meshtastic_NodeInfoLite lite2 = meshtastic_NodeInfoLite_init_default;
    TypeConversions::CopyUserToNodeInfoLite(&lite2, in);
    TEST_ASSERT_FALSE((lite2.bitfield & NODEINFO_BITFIELD_IS_LICENSED_MASK) != 0);
    meshtastic_User out2 = TypeConversions::ConvertToUser(&lite2);
    TEST_ASSERT_FALSE(out2.is_licensed);
}

void test_bitfield_unmessagable_present_and_true(void)
{
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    meshtastic_User in = makeUser("a", "a");
    in.has_is_unmessagable = true;
    in.is_unmessagable = true;
    TypeConversions::CopyUserToNodeInfoLite(&lite, in);
    TEST_ASSERT_TRUE((lite.bitfield & NODEINFO_BITFIELD_HAS_IS_UNMESSAGABLE_MASK) != 0);
    TEST_ASSERT_TRUE((lite.bitfield & NODEINFO_BITFIELD_IS_UNMESSAGABLE_MASK) != 0);
    meshtastic_User out = TypeConversions::ConvertToUser(&lite);
    TEST_ASSERT_TRUE(out.has_is_unmessagable);
    TEST_ASSERT_TRUE(out.is_unmessagable);
}

void test_bitfield_unmessagable_present_but_false(void)
{
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    meshtastic_User in = makeUser("a", "a");
    in.has_is_unmessagable = true;
    in.is_unmessagable = false;
    TypeConversions::CopyUserToNodeInfoLite(&lite, in);
    TEST_ASSERT_TRUE((lite.bitfield & NODEINFO_BITFIELD_HAS_IS_UNMESSAGABLE_MASK) != 0);
    TEST_ASSERT_FALSE((lite.bitfield & NODEINFO_BITFIELD_IS_UNMESSAGABLE_MASK) != 0);
    meshtastic_User out = TypeConversions::ConvertToUser(&lite);
    TEST_ASSERT_TRUE(out.has_is_unmessagable);
    TEST_ASSERT_FALSE(out.is_unmessagable);
}

void test_bitfield_unmessagable_absent(void)
{
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    meshtastic_User in = makeUser("a", "a");
    in.has_is_unmessagable = false;
    in.is_unmessagable = true; // explicitly true to make sure absence still wins
    TypeConversions::CopyUserToNodeInfoLite(&lite, in);
    TEST_ASSERT_FALSE((lite.bitfield & NODEINFO_BITFIELD_HAS_IS_UNMESSAGABLE_MASK) != 0);
    TEST_ASSERT_FALSE((lite.bitfield & NODEINFO_BITFIELD_IS_UNMESSAGABLE_MASK) != 0);
    meshtastic_User out = TypeConversions::ConvertToUser(&lite);
    TEST_ASSERT_FALSE(out.has_is_unmessagable);
    TEST_ASSERT_FALSE(out.is_unmessagable);
}

void test_copy_user_preserves_unrelated_bits(void)
{
    // Pre-set is_muted and is_key_manually_verified - CopyUserToNodeInfoLite
    // must not stomp them.
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    lite.bitfield = NODEINFO_BITFIELD_IS_MUTED_MASK | NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_MASK |
                    NODEINFO_BITFIELD_VIA_MQTT_MASK | NODEINFO_BITFIELD_IS_FAVORITE_MASK | NODEINFO_BITFIELD_IS_IGNORED_MASK;
    meshtastic_User in = makeUser("a", "a");
    TypeConversions::CopyUserToNodeInfoLite(&lite, in);
    TEST_ASSERT_TRUE((lite.bitfield & NODEINFO_BITFIELD_IS_MUTED_MASK) != 0);
    TEST_ASSERT_TRUE((lite.bitfield & NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_MASK) != 0);
    TEST_ASSERT_TRUE((lite.bitfield & NODEINFO_BITFIELD_VIA_MQTT_MASK) != 0);
    TEST_ASSERT_TRUE((lite.bitfield & NODEINFO_BITFIELD_IS_FAVORITE_MASK) != 0);
    TEST_ASSERT_TRUE((lite.bitfield & NODEINFO_BITFIELD_IS_IGNORED_MASK) != 0);
}

void test_bitfield_bits_are_independent(void)
{
    // Set just is_licensed, verify only that bit (and HAS_USER) is set.
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    meshtastic_User in = makeUser("a", "a");
    in.is_licensed = true;
    TypeConversions::CopyUserToNodeInfoLite(&lite, in);
    const uint32_t expected = NODEINFO_BITFIELD_HAS_USER_MASK | NODEINFO_BITFIELD_IS_LICENSED_MASK;
    TEST_ASSERT_EQUAL_HEX32(expected, lite.bitfield);
}

// ---------- public_key / hw_model / role pass-through ------------------------

void test_public_key_round_trip(void)
{
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    meshtastic_User in = makeUser("a", "a");
    in.public_key.size = 32;
    for (int i = 0; i < 32; ++i)
        in.public_key.bytes[i] = (uint8_t)(i ^ 0xA5);
    TypeConversions::CopyUserToNodeInfoLite(&lite, in);
    TEST_ASSERT_EQUAL_INT(32, lite.public_key.size);
    TEST_ASSERT_EQUAL_MEMORY(in.public_key.bytes, lite.public_key.bytes, 32);
    meshtastic_User out = TypeConversions::ConvertToUser(&lite);
    TEST_ASSERT_EQUAL_INT(32, out.public_key.size);
    TEST_ASSERT_EQUAL_MEMORY(in.public_key.bytes, out.public_key.bytes, 32);
}

void test_hw_model_and_role_round_trip(void)
{
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    meshtastic_User in = makeUser("a", "a");
    in.hw_model = meshtastic_HardwareModel_HELTEC_V3;
    in.role = meshtastic_Config_DeviceConfig_Role_ROUTER;
    TypeConversions::CopyUserToNodeInfoLite(&lite, in);
    TEST_ASSERT_EQUAL_INT(meshtastic_HardwareModel_HELTEC_V3, lite.hw_model);
    TEST_ASSERT_EQUAL_INT(meshtastic_Config_DeviceConfig_Role_ROUTER, lite.role);
    meshtastic_User out = TypeConversions::ConvertToUser(&lite);
    TEST_ASSERT_EQUAL_INT(meshtastic_HardwareModel_HELTEC_V3, out.hw_model);
    TEST_ASSERT_EQUAL_INT(meshtastic_Config_DeviceConfig_Role_ROUTER, out.role);
}

// ---------- ConvertToNodeInfo (3-arg) ----------------------------------------

void test_convert_to_node_info_thin_omits_position_and_metrics(void)
{
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    lite.num = 0xAA;
    meshtastic_NodeInfo info = TypeConversions::ConvertToNodeInfoThin(&lite);
    TEST_ASSERT_FALSE(info.has_position);
    TEST_ASSERT_FALSE(info.has_device_metrics);
}

void test_convert_to_node_info_3arg_with_position(void)
{
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    lite.num = 0xAA;
    meshtastic_PositionLite pos = meshtastic_PositionLite_init_default;
    pos.latitude_i = 374200000;
    pos.longitude_i = -1221000000;
    pos.altitude = 30;
    pos.time = 12345;
    meshtastic_NodeInfo info = TypeConversions::ConvertToNodeInfo(&lite, &pos, nullptr);
    TEST_ASSERT_TRUE(info.has_position);
    TEST_ASSERT_FALSE(info.has_device_metrics);
    TEST_ASSERT_EQUAL_INT32(374200000, info.position.latitude_i);
    TEST_ASSERT_EQUAL_INT32(-1221000000, info.position.longitude_i);
    TEST_ASSERT_EQUAL_INT32(30, info.position.altitude);
    TEST_ASSERT_EQUAL_UINT32(12345, info.position.time);
}

void test_convert_to_node_info_3arg_with_metrics(void)
{
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    lite.num = 0xAA;
    meshtastic_DeviceMetrics dm = meshtastic_DeviceMetrics_init_default;
    dm.battery_level = 88;
    dm.has_battery_level = true;
    dm.voltage = 3.71f;
    dm.has_voltage = true;
    meshtastic_NodeInfo info = TypeConversions::ConvertToNodeInfo(&lite, nullptr, &dm);
    TEST_ASSERT_FALSE(info.has_position);
    TEST_ASSERT_TRUE(info.has_device_metrics);
    TEST_ASSERT_EQUAL_INT(88, info.device_metrics.battery_level);
    TEST_ASSERT_TRUE(info.device_metrics.has_battery_level);
    TEST_ASSERT_EQUAL_FLOAT(3.71f, info.device_metrics.voltage);
}

void test_convert_to_node_info_3arg_null_inputs(void)
{
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    lite.num = 0xAA;
    meshtastic_NodeInfo info = TypeConversions::ConvertToNodeInfo(&lite, nullptr, nullptr);
    TEST_ASSERT_FALSE(info.has_position);
    TEST_ASSERT_FALSE(info.has_device_metrics);
}

void test_convert_to_node_info_extracts_bitfield_bools(void)
{
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    lite.num = 0xBB;
    lite.bitfield = NODEINFO_BITFIELD_VIA_MQTT_MASK | NODEINFO_BITFIELD_IS_FAVORITE_MASK | NODEINFO_BITFIELD_IS_IGNORED_MASK |
                    NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_MASK | NODEINFO_BITFIELD_IS_MUTED_MASK;
    meshtastic_NodeInfo info = TypeConversions::ConvertToNodeInfo(&lite, nullptr, nullptr);
    TEST_ASSERT_TRUE(info.via_mqtt);
    TEST_ASSERT_TRUE(info.is_favorite);
    TEST_ASSERT_TRUE(info.is_ignored);
    TEST_ASSERT_TRUE(info.is_key_manually_verified);
    TEST_ASSERT_TRUE(info.is_muted);
}

void test_convert_to_node_info_extracts_bitfield_bools_none_set(void)
{
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    lite.num = 0xBB;
    lite.bitfield = 0;
    meshtastic_NodeInfo info = TypeConversions::ConvertToNodeInfo(&lite, nullptr, nullptr);
    TEST_ASSERT_FALSE(info.via_mqtt);
    TEST_ASSERT_FALSE(info.is_favorite);
    TEST_ASSERT_FALSE(info.is_ignored);
    TEST_ASSERT_FALSE(info.is_key_manually_verified);
    TEST_ASSERT_FALSE(info.is_muted);
}

void test_convert_to_node_info_user_only_when_has_user_bit_set(void)
{
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;
    lite.num = 0x01;
    strcpy(lite.long_name, "Alpha");
    strcpy(lite.short_name, "A");
    // No HAS_USER bit -> user fields ignored on emit.
    meshtastic_NodeInfo info = TypeConversions::ConvertToNodeInfo(&lite, nullptr, nullptr);
    TEST_ASSERT_FALSE(info.has_user);

    lite.bitfield |= NODEINFO_BITFIELD_HAS_USER_MASK;
    meshtastic_NodeInfo info2 = TypeConversions::ConvertToNodeInfo(&lite, nullptr, nullptr);
    TEST_ASSERT_TRUE(info2.has_user);
    TEST_ASSERT_EQUAL_STRING("Alpha", info2.user.long_name);
    TEST_ASSERT_EQUAL_STRING("A", info2.user.short_name);
    TEST_ASSERT_EQUAL_STRING("!00000001", info2.user.id);
}

// Regression for UnitConversions::displaySafeFloat: drop non-finite values and clamp magnitude so a
// crafted telemetry float can't overflow Arduino String(float)'s fixed char[33].
static void test_displaySafeFloat_bounds_and_finiteness()
{
    // Non-finite -> 0
    TEST_ASSERT_EQUAL_FLOAT(0.0f, UnitConversions::displaySafeFloat(NAN));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, UnitConversions::displaySafeFloat(INFINITY));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, UnitConversions::displaySafeFloat(-INFINITY));
    // Huge magnitudes -> clamped to +/-1e9
    TEST_ASSERT_EQUAL_FLOAT(1e9f, UnitConversions::displaySafeFloat(FLT_MAX));
    TEST_ASSERT_EQUAL_FLOAT(-1e9f, UnitConversions::displaySafeFloat(-FLT_MAX));
    TEST_ASSERT_EQUAL_FLOAT(1e9f, UnitConversions::displaySafeFloat(3.0e30f));
    // In-range values pass through unchanged
    TEST_ASSERT_EQUAL_FLOAT(0.0f, UnitConversions::displaySafeFloat(0.0f));
    TEST_ASSERT_EQUAL_FLOAT(23.5f, UnitConversions::displaySafeFloat(23.5f));
    TEST_ASSERT_EQUAL_FLOAT(-40.0f, UnitConversions::displaySafeFloat(-40.0f));
    TEST_ASSERT_EQUAL_FLOAT(120000.0f, UnitConversions::displaySafeFloat(120000.0f));

    // The clamped output, formatted the way telemetry does, must fit Arduino String(float)'s char[33].
    const float attackers[] = {FLT_MAX, -FLT_MAX, 3.0e30f, 1.0e9f, -1.0e9f};
    for (float v : attackers) {
        char buf[33];
        int n = snprintf(buf, sizeof(buf), "%.2f", (double)UnitConversions::displaySafeFloat(v));
        TEST_ASSERT_TRUE_MESSAGE(n > 0 && n < (int)sizeof(buf), "clamped float would overflow String(float) char[33]");
    }
}

// ---------- entry point -------------------------------------------------------

void setup()
{
    delay(10);
    delay(2000);
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_copy_user_sets_has_user_bit);
    RUN_TEST(test_convert_to_user_id_derived_from_nodenum);
    RUN_TEST(test_convert_to_user_zero_fills_macaddr);
    RUN_TEST(test_long_name_short_passes_through);
    RUN_TEST(test_long_name_exact_24_fits);
    RUN_TEST(test_long_name_truncates_when_too_long);
    RUN_TEST(test_long_name_round_trip_to_wire);
    RUN_TEST(test_long_name_truncated_utf8_boundary_sanitized);
    RUN_TEST(test_wire_decode_accepts_legacy_39_byte_long_name);
    RUN_TEST(test_wire_decode_rejects_name_beyond_wire_limit);
    RUN_TEST(test_short_name_passes_through);
    RUN_TEST(test_short_name_truncates_when_too_long);
    RUN_TEST(test_bitfield_is_licensed_round_trip);
    RUN_TEST(test_bitfield_unmessagable_present_and_true);
    RUN_TEST(test_bitfield_unmessagable_present_but_false);
    RUN_TEST(test_bitfield_unmessagable_absent);
    RUN_TEST(test_copy_user_preserves_unrelated_bits);
    RUN_TEST(test_bitfield_bits_are_independent);
    RUN_TEST(test_public_key_round_trip);
    RUN_TEST(test_hw_model_and_role_round_trip);
    RUN_TEST(test_convert_to_node_info_thin_omits_position_and_metrics);
    RUN_TEST(test_convert_to_node_info_3arg_with_position);
    RUN_TEST(test_convert_to_node_info_3arg_with_metrics);
    RUN_TEST(test_convert_to_node_info_3arg_null_inputs);
    RUN_TEST(test_convert_to_node_info_extracts_bitfield_bools);
    RUN_TEST(test_convert_to_node_info_extracts_bitfield_bools_none_set);
    RUN_TEST(test_convert_to_node_info_user_only_when_has_user_bit_set);
    RUN_TEST(test_displaySafeFloat_bounds_and_finiteness);
    exit(UNITY_END());
}

void loop() {}
