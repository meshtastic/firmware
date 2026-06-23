// Unit tests for Channels::decryptForHash() - channel selection during inbound decode.
//
// Covers the explicit-vs-implicit modem-preset name hashing compatibility added in #10766
// (fixes #10764) plus the out-of-bounds index guard.
//
// Background: when use_preset is on, getName() expands a blank channel name to the modem
// preset display name (e.g. "LongFast"), so our locally generated hash is always the
// "explicit preset name" hash: xorHash(presetName) ^ xorHash(PSK). Some senders instead
// hash the literal empty string, producing xorHash("") ^ xorHash(PSK) == xorHash(PSK).
// decryptForHash() recovers that alternate "blank-name" hash so the two interoperate.

#include "DisplayFormatters.h"
#include "TestUtil.h"
#include "mesh/Channels.h"
#include "mesh/NodeDB.h"

#include <string.h>
#include <unity.h>

// Free function with external linkage defined in Channels.cpp (no header declaration).
uint8_t xorHash(const uint8_t *p, size_t len);

namespace
{
// A full-length (16 byte) PSK keeps getKey() from padding/expanding, so the expected hashes
// are simply xorHash() over these exact bytes.
const uint8_t TEST_PSK[16] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10};

// Long display name for ModemPreset LONG_FAST (see DisplayFormatters::getModemPresetDisplayName).
const char *PRESET_NAME = "LongFast";

ChannelHash presetNameXor()
{
    return xorHash((const uint8_t *)PRESET_NAME, strlen(PRESET_NAME));
}

// The hash a sender produces when it hashes "" directly: xorHash("") ^ xorHash(PSK) == xorHash(PSK).
ChannelHash blankSenderHash()
{
    return xorHash(TEST_PSK, sizeof(TEST_PSK));
}

// Install our single primary channel (index 0) with the given stored name and TEST_PSK,
// then recompute hashes[] the way a real config change would.
void installChannel0(const char *name)
{
    meshtastic_Channel &ch = channels.getByIndex(0);
    ch.has_settings = true;
    ch.role = meshtastic_Channel_Role_PRIMARY;

    memset(ch.settings.name, 0, sizeof(ch.settings.name));
    strncpy(ch.settings.name, name, sizeof(ch.settings.name) - 1);

    memset(ch.settings.psk.bytes, 0, sizeof(ch.settings.psk.bytes));
    memcpy(ch.settings.psk.bytes, TEST_PSK, sizeof(TEST_PSK));
    ch.settings.psk.size = sizeof(TEST_PSK);

    channels.onConfigChanged(); // recompute hashes[] via fixupChannel()/generateHash()
}

void usePresetLongFast()
{
    config.lora.use_preset = true;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
}
} // namespace

void setUp(void)
{
    // Fresh channel set (count == MAX_NUM_CHANNELS, ch0 = default primary) before each test.
    channels.initDefaults();
}

void tearDown(void) {}

// An exact local-hash match still decodes (no regression from the alias logic).
void test_exact_hash_match_still_decodes()
{
    usePresetLongFast();
    installChannel0(PRESET_NAME);

    ChannelHash local = (ChannelHash)channels.getHash(0);
    TEST_ASSERT_TRUE(channels.decryptForHash(0, local));
}

// The '>' -> '>=' fix: an index equal to getNumChannels() must be rejected before getHash()
// reads hashes[] out of bounds. Valid indices are 0 .. getNumChannels()-1.
void test_rejects_out_of_bounds_index()
{
    usePresetLongFast();
    installChannel0(PRESET_NAME);

    ChannelIndex oob = channels.getNumChannels();
    TEST_ASSERT_FALSE(channels.decryptForHash(oob, 0x00));
}

// Pins the derived-hash formula (and the ChannelHash-typed arithmetic that replaced the
// uint8_t narrowing cast): localHash ^ xorHash(presetName) == xorHash(PSK).
void test_blank_name_hash_algebra()
{
    usePresetLongFast();
    installChannel0(PRESET_NAME);

    ChannelHash local = (ChannelHash)channels.getHash(0);
    ChannelHash blank = local ^ presetNameXor();
    TEST_ASSERT_EQUAL_UINT8(blankSenderHash(), blank);
}

// Path A: stored name equals the preset name; a packet hashed with "" is accepted.
void test_pathA_explicit_preset_name_accepts_blank_hash()
{
    usePresetLongFast();
    installChannel0(PRESET_NAME); // stored name == preset name

    ChannelHash local = (ChannelHash)channels.getHash(0);
    ChannelHash blank = blankSenderHash();
    TEST_ASSERT_TRUE(local != blank); // genuine alias case, not an exact match
    TEST_ASSERT_TRUE(channels.decryptForHash(0, blank));
}

// Path B: stored name is blank (getName() expands it to the preset name), so a packet hashed
// with "" is still accepted via the non-standard blank-name path.
void test_pathB_blank_stored_name_accepts_blank_hash()
{
    usePresetLongFast();
    installChannel0(""); // blank stored name

    ChannelHash local = (ChannelHash)channels.getHash(0);
    ChannelHash blank = blankSenderHash();
    TEST_ASSERT_TRUE(local != blank);
    TEST_ASSERT_TRUE(channels.decryptForHash(0, blank));
}

// A hash that matches neither the exact local hash nor the blank-name alias is rejected.
void test_rejects_genuinely_wrong_hash()
{
    usePresetLongFast();
    installChannel0(PRESET_NAME);

    ChannelHash local = (ChannelHash)channels.getHash(0);
    ChannelHash blank = blankSenderHash();
    ChannelHash wrong = (ChannelHash)(local + 1);
    if (wrong == blank)
        wrong = (ChannelHash)(local + 2);
    TEST_ASSERT_FALSE(channels.decryptForHash(0, wrong));
}

// With use_preset disabled the alias path is skipped: a blank-name hash is rejected, while an
// exact match still decodes.
void test_alias_skipped_when_use_preset_false()
{
    usePresetLongFast();
    installChannel0(PRESET_NAME); // explicit name -> local hash is preset-name based regardless of use_preset

    config.lora.use_preset = false;

    ChannelHash local = (ChannelHash)channels.getHash(0);
    TEST_ASSERT_FALSE(channels.decryptForHash(0, blankSenderHash()));
    TEST_ASSERT_TRUE(channels.decryptForHash(0, local));
}

void setup()
{
    delay(10);
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_exact_hash_match_still_decodes);
    RUN_TEST(test_rejects_out_of_bounds_index);
    RUN_TEST(test_blank_name_hash_algebra);
    RUN_TEST(test_pathA_explicit_preset_name_accepts_blank_hash);
    RUN_TEST(test_pathB_blank_stored_name_accepts_blank_hash);
    RUN_TEST(test_rejects_genuinely_wrong_hash);
    RUN_TEST(test_alias_skipped_when_use_preset_false);
    exit(UNITY_END());
}

void loop() {}
