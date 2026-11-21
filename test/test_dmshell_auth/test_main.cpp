// Unit tests for DMShellModule::isAuthorizedPacket() in src/modules/DMShell.cpp, and for the
// Security.DMShell opt-in that loadConfig() in src/platform/portduino/PortduinoGlue.cpp reads.
//
// Why this behavior is required: DMShell runs an interactive shell on the meshtasticd host. That is
// more than control of the node, so it is off unless config.yaml turns it on, and once on it answers
// only a PKI DM whose sender key is in AdminKeys - the one credential a local API client cannot
// forge. For a local packet (from == 0) MeshService::handleToRadio() keeps the client's own
// pki_encrypted and public_key, and Router::perhapsDecode() returns before clearing them because
// the packet is already decoded, so neither field says anything about who sent it.
//
// Regressions guarded if these assertions are deleted or relaxed:
//   - from == 0 authorized again (it used to be, unless is_managed): anything that can reach the API
//     on TCP 4403 gets a shell on the host by setting pki_encrypted on a self-addressed packet.
//   - the legacy admin channel authorizing a shell: a PKI DM decodes as channel index 0, so with the
//     primary channel named "admin" every node holding any key would be let in.
//   - Security.DMShell defaulting to on, or a later config.d file unable to turn it back off.
#include "Arduino.h"
#include "TestUtil.h"
#include "mesh/AdminKeys.h"
#include "mesh/Channels.h"
#include "mesh/NodeDB.h"
#include "modules/DMShell.h"
#include "platform/portduino/PortduinoGlue.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unity.h>

#if defined(MESHTASTIC_HAS_DMSHELL)

static constexpr NodeNum REMOTE_NODE = 0x12345678;

static void setAdminKey(uint8_t fill)
{
    memset(config.security.admin_key[0].bytes, fill, 32);
    config.security.admin_key[0].size = 32;
    config.security.admin_key_count = 1;
}

static meshtastic_MeshPacket pkiPacketFrom(NodeNum from, uint8_t keyFill)
{
    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = from;
    mp.to = REMOTE_NODE + 1;
    mp.channel = 0;
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    mp.decoded.portnum = meshtastic_PortNum_REMOTE_SHELL_APP;
    mp.pki_encrypted = true;
    memset(mp.public_key.bytes, keyFill, 32);
    mp.public_key.size = 32;
    return mp;
}

// Loads body as a config file, the way portduinoSetup() loads config.yaml and each config.d entry.
static bool loadYaml(const char *body)
{
    const std::string path = "/tmp/meshtastic-dmshell-auth.yaml";
    FILE *f = fopen(path.c_str(), "w");
    TEST_ASSERT_NOT_NULL(f);
    fputs(body, f);
    fclose(f);
    const bool loaded = loadConfig(path.c_str());
    remove(path.c_str());
    return loaded;
}

void setUp(void)
{
    for (size_t slot = 0; slot < AdminKeys::PROTOBUF_SLOTS; slot++) {
        memset(config.security.admin_key[slot].bytes, 0, 32);
        config.security.admin_key[slot].size = 0;
    }
    config.security.admin_key_count = 0;
    config.security.is_managed = false;
    config.security.admin_channel_enabled = false;
    portduino_config.admin_keys.clear();
    portduino_config.dmshell_enabled = false;
}

void tearDown(void)
{
    setUp();
}

// --- isAuthorizedPacket ---

void test_admin_key_sender_is_authorized()
{
    setAdminKey(0xAA);
    TEST_ASSERT_TRUE(DMShellModule::isAuthorizedPacket(pkiPacketFrom(REMOTE_NODE, 0xAA)));
}

void test_unknown_key_sender_is_refused()
{
    setAdminKey(0xAA);
    TEST_ASSERT_FALSE(DMShellModule::isAuthorizedPacket(pkiPacketFrom(REMOTE_NODE, 0xBB)));
}

void test_admin_key_without_pki_is_refused()
{
    setAdminKey(0xAA);
    meshtastic_MeshPacket mp = pkiPacketFrom(REMOTE_NODE, 0xAA);
    mp.pki_encrypted = false;
    TEST_ASSERT_FALSE(DMShellModule::isAuthorizedPacket(mp));
}

void test_local_client_is_refused_even_with_forged_admin_key()
{
    // Every field here is the client's to choose, including a copy of a real admin's public key.
    setAdminKey(0xAA);
    TEST_ASSERT_FALSE(DMShellModule::isAuthorizedPacket(pkiPacketFrom(0, 0xAA)));
}

void test_local_client_is_refused_when_unmanaged()
{
    TEST_ASSERT_FALSE(config.security.is_managed);
    TEST_ASSERT_FALSE(DMShellModule::isAuthorizedPacket(pkiPacketFrom(0, 0x00)));
}

void test_admin_channel_does_not_authorize_a_shell()
{
    channels.initDefaults();
    meshtastic_Channel &primary = channels.getByIndex(0);
    strncpy(primary.settings.name, Channels::adminChannel, sizeof(primary.settings.name) - 1);
    config.security.admin_channel_enabled = true;

    TEST_ASSERT_FALSE(DMShellModule::isAuthorizedPacket(pkiPacketFrom(REMOTE_NODE, 0xBB)));

    channels.initDefaults();
}

// --- Security.DMShell in config.yaml ---

void test_dmshell_is_off_by_default()
{
    TEST_ASSERT_TRUE(loadYaml("Security:\n  AdminKeys: []\n"));
    TEST_ASSERT_FALSE(portduino_config.dmshell_enabled);
}

void test_dmshell_opt_in_and_later_file_turns_it_off()
{
    TEST_ASSERT_TRUE(loadYaml("Security:\n  DMShell: true\n"));
    TEST_ASSERT_TRUE(portduino_config.dmshell_enabled);

    // A config.d file that does not mention it leaves the earlier choice alone...
    TEST_ASSERT_TRUE(loadYaml("General:\n  MaxNodes: 200\n"));
    TEST_ASSERT_TRUE(portduino_config.dmshell_enabled);

    // ...and one that does wins.
    TEST_ASSERT_TRUE(loadYaml("Security:\n  DMShell: false\n"));
    TEST_ASSERT_FALSE(portduino_config.dmshell_enabled);
}

#endif

// --- Unity lifecycle ---

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
#if defined(MESHTASTIC_HAS_DMSHELL)
    RUN_TEST(test_admin_key_sender_is_authorized);
    RUN_TEST(test_unknown_key_sender_is_refused);
    RUN_TEST(test_admin_key_without_pki_is_refused);
    RUN_TEST(test_local_client_is_refused_even_with_forged_admin_key);
    RUN_TEST(test_local_client_is_refused_when_unmanaged);
    RUN_TEST(test_admin_channel_does_not_authorize_a_shell);
    RUN_TEST(test_dmshell_is_off_by_default);
    RUN_TEST(test_dmshell_opt_in_and_later_file_turns_it_off);
#endif
    exit(UNITY_END());
}

void loop() {}
