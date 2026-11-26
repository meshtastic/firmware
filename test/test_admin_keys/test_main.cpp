// Unit tests for AdminKeys::count/keyAt/isAuthorized in src/mesh/AdminKeys.cpp, and for the
// admin-key base64 codec (adminKeyFromBase64 / adminKeyToBase64) in
// src/platform/portduino/PortduinoGlue.cpp.
//
// Why this behavior is required: four sites decide who may administer a node - the PKC gate in
// AdminModule::handleReceivedProtobuf, the same gate in DMShell::isAuthorized, the is_managed
// guard in AdminModule::handleSetConfig, and the admin-key trial-decrypt fallback in
// Router::perhapsDecode. Each one used to unroll config.security.admin_key[0..2] by hand, so a key
// source other than that array - meshtasticd's Security.AdminKeys, which may carry more keys than
// the three the protobuf holds - would have authorized at some sites and not others. AdminKeys is
// the single list all four read, so it has to answer for both sources at once.
//
// Regressions guarded if these assertions are deleted or relaxed:
//   - keyAt() enumerating sparsely: config.security.admin_key can have a populated slot after an
//     empty one (a client clearing the middle key), and admin_key_count is not maintained at every
//     write site. An enumeration that stops at the first empty slot, or trusts admin_key_count,
//     silently drops a configured key - Router's fallback would never try it, and an authorized
//     admin could not reach the node.
//   - host keys past the protobuf array going unauthorized: applyHostKeys() mirrors only the first
//     PROTOBUF_SLOTS of them into config.security, so the rest exist nowhere else.
//   - applyHostKeys() either not clearing the slots the host file no longer fills (a key removed
//     from the file would keep working from the persisted config) or clearing slots when the host
//     configured no keys at all (every non-meshtasticd build, and every host that sets its admin
//     keys from a client, would lose them).
//   - adminKeyFromBase64 accepting a key that is not exactly 32 canonical bytes. Padding a short
//     key with zeros, or truncating a long one, installs an admin key nobody holds, or one that
//     collides with another operator's.
#include "Arduino.h"
#include "TestUtil.h"
#include "mesh/AdminKeys.h"
#include "mesh/NodeDB.h"
#include "platform/portduino/PortduinoGlue.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <unity.h>

// A 32-byte key whose every byte is fill, so a key is identifiable from keyAt()[0] alone.
static std::array<uint8_t, 32> keyOf(uint8_t fill)
{
    std::array<uint8_t, 32> key;
    key.fill(fill);
    return key;
}

static void setProtobufSlot(size_t slot, uint8_t fill)
{
    memset(config.security.admin_key[slot].bytes, fill, 32);
    config.security.admin_key[slot].size = 32;
}

static void clearProtobufSlot(size_t slot)
{
    memset(config.security.admin_key[slot].bytes, 0, 32);
    config.security.admin_key[slot].size = 0;
}

void setUp(void)
{
    for (size_t slot = 0; slot < AdminKeys::PROTOBUF_SLOTS; slot++)
        clearProtobufSlot(slot);
    config.security.admin_key_count = 0;
    portduino_config.admin_keys.clear();
}

void tearDown(void)
{
    setUp();
}

// --- No keys configured ---

void test_no_keys_authorizes_nothing()
{
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)AdminKeys::count());
    TEST_ASSERT_FALSE(AdminKeys::any());
    TEST_ASSERT_NULL(AdminKeys::keyAt(0));
    TEST_ASSERT_FALSE(AdminKeys::isAuthorized(keyOf(0x11).data()));
}

void test_null_public_key_is_not_authorized()
{
    setProtobufSlot(0, 0x11);
    TEST_ASSERT_FALSE(AdminKeys::isAuthorized(nullptr));
}

// --- Keys in config.security.admin_key ---

void test_protobuf_key_is_authorized()
{
    setProtobufSlot(0, 0xA1);
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)AdminKeys::count());
    TEST_ASSERT_TRUE(AdminKeys::any());
    TEST_ASSERT_TRUE(AdminKeys::isAuthorized(keyOf(0xA1).data()));
    TEST_ASSERT_FALSE(AdminKeys::isAuthorized(keyOf(0xA2).data()));
}

void test_sparse_slots_enumerate_densely()
{
    // Slot 1 left empty: keyAt() must not stop there, and must not report it as a key.
    setProtobufSlot(0, 0xA1);
    setProtobufSlot(2, 0xA3);
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)AdminKeys::count());
    TEST_ASSERT_NOT_NULL(AdminKeys::keyAt(0));
    TEST_ASSERT_EQUAL_HEX8(0xA1, AdminKeys::keyAt(0)[0]);
    TEST_ASSERT_NOT_NULL(AdminKeys::keyAt(1));
    TEST_ASSERT_EQUAL_HEX8(0xA3, AdminKeys::keyAt(1)[0]);
    TEST_ASSERT_NULL(AdminKeys::keyAt(2));
    TEST_ASSERT_TRUE(AdminKeys::isAuthorized(keyOf(0xA3).data()));
}

void test_stale_admin_key_count_is_not_trusted()
{
    // A count of zero against a populated slot is what several write sites leave behind.
    setProtobufSlot(0, 0xA1);
    config.security.admin_key_count = 0;
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)AdminKeys::count());
    TEST_ASSERT_TRUE(AdminKeys::isAuthorized(keyOf(0xA1).data()));
}

void test_short_slot_is_not_a_key()
{
    // Only a full 32-byte key counts; a partially written slot must not authorize anything.
    memset(config.security.admin_key[0].bytes, 0xA1, 32);
    config.security.admin_key[0].size = 31;
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)AdminKeys::count());
    TEST_ASSERT_FALSE(AdminKeys::isAuthorized(keyOf(0xA1).data()));
}

// --- Host keys from config.yaml, past the protobuf array ---

void test_host_keys_past_protobuf_slots_are_authorized()
{
    // Five host keys, the first three of which NodeDB::loadFromDisk() mirrored into the protobuf.
    for (uint8_t i = 0; i < 5; i++)
        portduino_config.admin_keys.push_back(keyOf(0xB0 + i));
    for (size_t slot = 0; slot < AdminKeys::PROTOBUF_SLOTS; slot++)
        setProtobufSlot(slot, 0xB0 + (uint8_t)slot);

    TEST_ASSERT_EQUAL_UINT32(5, (uint32_t)AdminKeys::count());
    for (uint8_t i = 0; i < 5; i++)
        TEST_ASSERT_TRUE(AdminKeys::isAuthorized(keyOf(0xB0 + i).data()));
    TEST_ASSERT_FALSE(AdminKeys::isAuthorized(keyOf(0xB5).data()));

    TEST_ASSERT_NOT_NULL(AdminKeys::keyAt(4));
    TEST_ASSERT_EQUAL_HEX8(0xB4, AdminKeys::keyAt(4)[0]);
    TEST_ASSERT_NULL(AdminKeys::keyAt(5));
}

void test_host_keys_within_protobuf_slots_are_not_counted_twice()
{
    // Two host keys both fit the protobuf array, so they are the same two keys, not four.
    portduino_config.admin_keys.push_back(keyOf(0xC1));
    portduino_config.admin_keys.push_back(keyOf(0xC2));
    setProtobufSlot(0, 0xC1);
    setProtobufSlot(1, 0xC2);
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)AdminKeys::count());
}

void test_apply_host_keys_mirrors_into_protobuf_slots()
{
    // What NodeDB::loadFromDisk() does at boot: the host's keys take the protobuf slots, whatever a
    // client or a remote admin left in them.
    for (uint8_t i = 0; i < 5; i++)
        portduino_config.admin_keys.push_back(keyOf(0xD0 + i));
    for (size_t slot = 0; slot < AdminKeys::PROTOBUF_SLOTS; slot++)
        setProtobufSlot(slot, 0xEE);

    AdminKeys::applyHostKeys();
    TEST_ASSERT_EQUAL_UINT32(AdminKeys::PROTOBUF_SLOTS, (uint32_t)config.security.admin_key_count);
    TEST_ASSERT_EQUAL_HEX8(0xD0, config.security.admin_key[0].bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0xD2, config.security.admin_key[2].bytes[0]);
    TEST_ASSERT_EQUAL_UINT32(5, (uint32_t)AdminKeys::count());
    TEST_ASSERT_FALSE(AdminKeys::isAuthorized(keyOf(0xEE).data()));
}

void test_apply_host_keys_clears_slots_the_host_no_longer_fills()
{
    // Removing a key from the host file has to remove its access, not leave it in the persisted
    // config where it keeps working.
    portduino_config.admin_keys.push_back(keyOf(0xD0));
    for (size_t slot = 0; slot < AdminKeys::PROTOBUF_SLOTS; slot++)
        setProtobufSlot(slot, 0xD0 + (uint8_t)slot);

    AdminKeys::applyHostKeys();
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)config.security.admin_key_count);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)config.security.admin_key[1].size);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)config.security.admin_key[2].size);
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)AdminKeys::count());
    TEST_ASSERT_FALSE(AdminKeys::isAuthorized(keyOf(0xD1).data()));
}

void test_apply_host_keys_leaves_config_alone_when_host_has_none()
{
    // The common case: no Security.AdminKeys in config.yaml, so keys set by a client must survive.
    setProtobufSlot(1, 0xEE);
    AdminKeys::applyHostKeys();
    TEST_ASSERT_EQUAL_UINT32(32, (uint32_t)config.security.admin_key[1].size);
    TEST_ASSERT_TRUE(AdminKeys::isAuthorized(keyOf(0xEE).data()));
}

// --- base64 codec ---

void test_base64_round_trip()
{
    const std::string text = "rvzaPCZmlh1nWnqNKUFHj0dLBJJIRJhRrkZmaFLHCVY=";
    std::array<uint8_t, 32> key{};
    TEST_ASSERT_TRUE(adminKeyFromBase64(text, key));
    TEST_ASSERT_EQUAL_STRING(text.c_str(), adminKeyToBase64(key).c_str());
}

void test_base64_accepts_unpadded_whitespace_and_urlsafe()
{
    std::array<uint8_t, 32> padded{};
    std::array<uint8_t, 32> other{};
    TEST_ASSERT_TRUE(adminKeyFromBase64("rvzaPCZmlh1nWnqNKUFHj0dLBJJIRJhRrkZmaFLHCVY=", padded));
    TEST_ASSERT_TRUE(adminKeyFromBase64("rvzaPCZmlh1nWnqNKUFHj0dLBJJIRJhRrkZmaFLHCVY", other));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(padded.data(), other.data(), 32);
    TEST_ASSERT_TRUE(adminKeyFromBase64("  rvzaPCZmlh1nWnqNKUFHj0dLBJJIRJhRrkZmaFLHCVY=\n", other));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(padded.data(), other.data(), 32);
    // The same 32 bytes in the base64url alphabet: '+' and '/' become '-' and '_'.
    std::array<uint8_t, 32> urlsafe{};
    TEST_ASSERT_TRUE(adminKeyFromBase64("NGC-MSAeaf7aoO7ouZl_XHwpmf2v5ZMlPNZUr0361xQ=", urlsafe));
    std::array<uint8_t, 32> standard{};
    TEST_ASSERT_TRUE(adminKeyFromBase64("NGC+MSAeaf7aoO7ouZl/XHwpmf2v5ZMlPNZUr0361xQ=", standard));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(standard.data(), urlsafe.data(), 32);
}

void test_base64_rejects_wrong_length()
{
    std::array<uint8_t, 32> key{};
    TEST_ASSERT_FALSE(adminKeyFromBase64("", key));
    TEST_ASSERT_FALSE(adminKeyFromBase64("AAAA", key));
    // 40 characters, so 30 bytes: short of a key, and not short by a whole base64 group.
    TEST_ASSERT_FALSE(adminKeyFromBase64("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", key));
    // 47 characters, so 35 bytes: one base64 group too many.
    TEST_ASSERT_FALSE(adminKeyFromBase64("rvzaPCZmlh1nWnqNKUFHj0dLBJJIRJhRrkZmaFLHCVYAAAA=", key));
}

void test_base64_rejects_bad_characters_and_noncanonical_tail()
{
    std::array<uint8_t, 32> key{};
    TEST_ASSERT_FALSE(adminKeyFromBase64("rvzaPCZmlh1nWnqNKUFHj0dLBJJIRJhRrkZmaFLHCV*=", key));
    // 44 characters, but the last one carries bits past the 32nd byte.
    TEST_ASSERT_FALSE(adminKeyFromBase64("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAB=", key));
}

// --- Unity lifecycle ---

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_no_keys_authorizes_nothing);
    RUN_TEST(test_null_public_key_is_not_authorized);
    RUN_TEST(test_protobuf_key_is_authorized);
    RUN_TEST(test_sparse_slots_enumerate_densely);
    RUN_TEST(test_stale_admin_key_count_is_not_trusted);
    RUN_TEST(test_short_slot_is_not_a_key);
    RUN_TEST(test_host_keys_past_protobuf_slots_are_authorized);
    RUN_TEST(test_host_keys_within_protobuf_slots_are_not_counted_twice);
    RUN_TEST(test_apply_host_keys_mirrors_into_protobuf_slots);
    RUN_TEST(test_apply_host_keys_clears_slots_the_host_no_longer_fills);
    RUN_TEST(test_apply_host_keys_leaves_config_alone_when_host_has_none);
    RUN_TEST(test_base64_round_trip);
    RUN_TEST(test_base64_accepts_unpadded_whitespace_and_urlsafe);
    RUN_TEST(test_base64_rejects_wrong_length);
    RUN_TEST(test_base64_rejects_bad_characters_and_noncanonical_tail);
    exit(UNITY_END());
}

void loop() {}
