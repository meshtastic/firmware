/*
 * Lockdown redaction.
 *
 * PhoneAPI::getFromRadio() withholds sensitive config from a connection that has not proven the
 * lockdown passphrase. That is the property the whole feature rests on, and until now nothing
 * asserted it: the gates are #ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL, which a normal native
 * build does not define, so they compiled out of every test.
 *
 * [env:coverage-lockdown] turns both lockdown flags on for this suite. No nRF52 crypto is
 * needed - EncryptedStorage carries non-ARCH_NRF52 fallbacks, and isLockdownActive() is just
 * "does /prefs/.dek exist", which these tests create and remove. Per-connection authorization
 * is then driven directly through setAdminAuthorized().
 *
 * Every test asserts both directions. A redaction test that only checks the redacted case
 * passes just as happily against a build that returns nothing to anyone.
 */

#include "configuration.h"

// This suite runs in two shards: once under [env:coverage-lockdown], which defines the lockdown
// flags and where the assertions below are meaningful, and once under plain [env:coverage] as
// part of the general pool, where the gates - and PhoneAPI::setAdminAuthorized() itself - do not
// exist. Compile to a single ignored case there rather than breaking that shard.
#ifdef MESHTASTIC_PHONEAPI_ACCESS_CONTROL

#include "FSCommon.h"
#include "TestUtil.h"
#include "mesh-pb-constants.h"
#include "mesh/MeshService.h"
#include "mesh/NodeDB.h"
#include "mesh/PhoneAPI.h"
#include "security/EncryptedStorage.h"
#include <cstring>
#include <unity.h>
#include <vector>

namespace
{

/// PhoneAPI::handleStartConfig() observes the global MeshService, so one has to exist.
class ScopedMeshService
{
  public:
    ScopedMeshService() : previous(service) { service = &instance; }
    ~ScopedMeshService() { service = previous; }

  private:
    MeshService instance;
    MeshService *previous;
};

/// Same for the node DB, which the config dump walks.
class ScopedNodeDB
{
  public:
    ScopedNodeDB() : previous(nodeDB) { nodeDB = &instance; }
    ~ScopedNodeDB() { nodeDB = previous; }

  private:
    NodeDB instance;
    NodeDB *previous;
};

class PhoneAPITestShim : public PhoneAPI
{
  protected:
    bool checkIsConnected() override { return true; }
};

/// isLockdownActive() is FSCom.exists("/prefs/.dek"). Create it to put the device "in lockdown"
/// without provisioning anything, which needs crypto this build does not have.
void setLockdownActive(bool active)
{
    const char *dek = "/prefs/.dek";
    if (active) {
        FSCom.mkdir("/prefs");
        auto f = FSCom.open(dek, FILE_O_WRITE);
        if (f) {
            const uint8_t filler[8] = {0};
            f.write(filler, sizeof(filler));
            f.close();
        }
    } else {
        FSCom.remove(dek);
    }
}

/// Drive want_config and collect every FromRadio the device emits for it.
///
/// Authorization is applied *after* handleToRadio(): starting a config session resets the
/// connection's auth slot (the security boundary is the physical link, not the handshake), so
/// anything set before this call is discarded.
std::vector<meshtastic_FromRadio> pumpConfig(PhoneAPI &api, bool authorized)
{
    meshtastic_ToRadio request = meshtastic_ToRadio_init_zero;
    request.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
    // A plain nonce, not SPECIAL_NONCE_ONLY_CONFIG: that special value skips the node DB for
    // everyone, which would make the "other nodes withheld" assertions vacuous.
    request.want_config_id = 42;
    uint8_t requestBytes[meshtastic_ToRadio_size];
    const size_t requestSize = pb_encode_to_bytes(requestBytes, sizeof(requestBytes), &meshtastic_ToRadio_msg, &request);
    api.handleToRadio(requestBytes, requestSize);
    api.setAdminAuthorized(authorized);

    std::vector<meshtastic_FromRadio> out;
    for (unsigned i = 0; i < 256; ++i) {
        uint8_t responseBytes[meshtastic_FromRadio_size];
        const size_t n = api.getFromRadio(responseBytes);
        if (n == 0)
            break;
        meshtastic_FromRadio fr = meshtastic_FromRadio_init_zero;
        if (pb_decode_from_bytes(responseBytes, n, &meshtastic_FromRadio_msg, &fr))
            out.push_back(fr);
    }
    return out;
}

/// The single config payload of the given variant, if the device sent one.
bool findConfig(const std::vector<meshtastic_FromRadio> &frames, pb_size_t variant, meshtastic_Config &out)
{
    for (const auto &f : frames)
        if (f.which_payload_variant == meshtastic_FromRadio_config_tag && f.config.which_payload_variant == variant) {
            out = f.config;
            return true;
        }
    return false;
}

void primeSecurityConfig()
{
    config.security = meshtastic_Config_SecurityConfig_init_zero;
    config.security.public_key.size = 32;
    memset(config.security.public_key.bytes, 0xA1, 32);
    config.security.private_key.size = 32;
    memset(config.security.private_key.bytes, 0xB2, 32);
    config.security.admin_key_count = 1;
    config.security.admin_key[0].size = 32;
    memset(config.security.admin_key[0].bytes, 0xC3, 32);
}

/// Saved globals, restored after every test.
meshtastic_Config_SecurityConfig savedSecurity;
meshtastic_Config_NetworkConfig savedNetwork;
meshtastic_Config_BluetoothConfig savedBluetooth;
meshtastic_Config_LoRaConfig savedLora;
meshtastic_ModuleConfig_MQTTConfig savedMqtt;
uint32_t savedMyNodeNum;

} // namespace

// Unity longjmps out of a failing test, so anything after the failing assertion never runs.
// State has to be reset here or one red test poisons every test after it.
void setUp(void)
{
    savedSecurity = config.security;
    savedNetwork = config.network;
    savedBluetooth = config.bluetooth;
    savedLora = config.lora;
    savedMqtt = moduleConfig.mqtt;
    savedMyNodeNum = myNodeInfo.my_node_num;
}

void tearDown(void)
{
    setLockdownActive(false);
    config.security = savedSecurity;
    config.network = savedNetwork;
    config.bluetooth = savedBluetooth;
    config.lora = savedLora;
    moduleConfig.mqtt = savedMqtt;
    myNodeInfo.my_node_num = savedMyNodeNum;
}

// The headline property: an unauthorized connection gets an empty SecurityConfig - no private
// key, no public key, no admin keys - and an authorized one gets the real thing.
static void test_security_config_redacted_until_authorized(void)
{
    ScopedMeshService scopedService;
    ScopedNodeDB scopedNodeDB;
    setLockdownActive(true);
    primeSecurityConfig();

    PhoneAPITestShim unauth;
    meshtastic_Config got = meshtastic_Config_init_zero;
    TEST_ASSERT_TRUE_MESSAGE(findConfig(pumpConfig(unauth, false), meshtastic_Config_security_tag, got),
                             "security config must still be sent, just emptied");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, got.payload_variant.security.private_key.size, "private key must not leak");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, got.payload_variant.security.public_key.size, "public key must not leak");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, got.payload_variant.security.admin_key_count, "admin keys must not leak");
    unauth.close();

    PhoneAPITestShim auth;
    got = meshtastic_Config_init_zero;
    TEST_ASSERT_TRUE(findConfig(pumpConfig(auth, true), meshtastic_Config_security_tag, got));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(32, got.payload_variant.security.private_key.size,
                                   "an authorized client must still receive the real config");
    TEST_ASSERT_EQUAL_UINT(1, got.payload_variant.security.admin_key_count);
    auth.close();
}

// The pairing PIN is a shared secret. PhoneAPI zeroes fixed_pin for an unauthorized client.
static void test_bluetooth_fixed_pin_redacted_until_authorized(void)
{
    ScopedMeshService scopedService;
    ScopedNodeDB scopedNodeDB;
    setLockdownActive(true);
    config.bluetooth.fixed_pin = 123456;

    PhoneAPITestShim unauth;
    meshtastic_Config got = meshtastic_Config_init_zero;
    TEST_ASSERT_TRUE(findConfig(pumpConfig(unauth, false), meshtastic_Config_bluetooth_tag, got));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, got.payload_variant.bluetooth.fixed_pin, "pairing PIN must not leak");
    unauth.close();

    PhoneAPITestShim auth;
    got = meshtastic_Config_init_zero;
    TEST_ASSERT_TRUE(findConfig(pumpConfig(auth, true), meshtastic_Config_bluetooth_tag, got));
    TEST_ASSERT_EQUAL_UINT(123456, got.payload_variant.bluetooth.fixed_pin);
    auth.close();
}

// WiFi credentials sit in the network config.
static void test_network_config_redacted_until_authorized(void)
{
    ScopedMeshService scopedService;
    ScopedNodeDB scopedNodeDB;
    setLockdownActive(true);
    config.network = meshtastic_Config_NetworkConfig_init_zero;
    strncpy(config.network.wifi_psk, "hunter2hunter2", sizeof(config.network.wifi_psk) - 1);
    strncpy(config.network.wifi_ssid, "meshnet", sizeof(config.network.wifi_ssid) - 1);

    PhoneAPITestShim unauth;
    meshtastic_Config got = meshtastic_Config_init_zero;
    TEST_ASSERT_TRUE(findConfig(pumpConfig(unauth, false), meshtastic_Config_network_tag, got));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", got.payload_variant.network.wifi_psk, "wifi psk must not leak");
    unauth.close();

    PhoneAPITestShim auth;
    got = meshtastic_Config_init_zero;
    TEST_ASSERT_TRUE(findConfig(pumpConfig(auth, true), meshtastic_Config_network_tag, got));
    TEST_ASSERT_EQUAL_STRING("hunter2hunter2", got.payload_variant.network.wifi_psk);
    auth.close();
}

// Channels carry the PSKs. An unauthorized client gets zeroed entries, but the state machine
// must still advance or the client never reaches config_complete.
static void test_channels_redacted_until_authorized(void)
{
    ScopedMeshService scopedService;
    ScopedNodeDB scopedNodeDB;
    setLockdownActive(true);

    PhoneAPITestShim unauth;
    auto frames = pumpConfig(unauth, false);
    bool sawChannel = false, sawComplete = false;
    for (const auto &f : frames) {
        if (f.which_payload_variant == meshtastic_FromRadio_channel_tag) {
            sawChannel = true;
            TEST_ASSERT_EQUAL_UINT_MESSAGE(0, f.channel.settings.psk.size, "channel PSK must not leak");
            TEST_ASSERT_EQUAL_STRING_MESSAGE("", f.channel.settings.name, "channel name must not leak");
        }
        if (f.which_payload_variant == meshtastic_FromRadio_config_complete_id_tag)
            sawComplete = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(sawChannel, "channels must still be emitted, just emptied");
    TEST_ASSERT_TRUE_MESSAGE(sawComplete, "redaction must not stall the handshake");
    unauth.close();
}

// With lockdown inactive the gates must be inert: a capable build that was never provisioned
// has to behave exactly like stock firmware.
//
// Name length is deliberate. A Lob API key is test_ or live_ followed by 35 characters, so a
// test function whose name happens to be test_ plus exactly 35 more trips trufflehog.
static void test_nothing_is_redacted_while_lockdown_is_inactive(void)
{
    ScopedMeshService scopedService;
    ScopedNodeDB scopedNodeDB;
    setLockdownActive(false);
    primeSecurityConfig();

    PhoneAPITestShim api;
    meshtastic_Config got = meshtastic_Config_init_zero;
    TEST_ASSERT_TRUE(findConfig(pumpConfig(api, false), meshtastic_Config_security_tag, got));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(32, got.payload_variant.security.private_key.size,
                                   "an unprovisioned lockdown build must not redact anything");
    api.close();
}

// DeviceMetadata is a fingerprint vector - firmware version, hw model, role, which modules are
// excluded. PhoneAPI wipes the whole struct for an unauthorized client.
static void test_device_metadata_wiped_until_authorized(void)
{
    ScopedMeshService scopedService;
    ScopedNodeDB scopedNodeDB;
    setLockdownActive(true);

    PhoneAPITestShim unauth;
    bool sawMetadata = false;
    for (const auto &f : pumpConfig(unauth, false))
        if (f.which_payload_variant == meshtastic_FromRadio_metadata_tag) {
            sawMetadata = true;
            TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)f.metadata.hw_model, "hw_model must not leak");
            TEST_ASSERT_EQUAL_STRING_MESSAGE("", f.metadata.firmware_version, "firmware version must not leak");
        }
    TEST_ASSERT_TRUE_MESSAGE(sawMetadata, "metadata frame must still be sent, just emptied");
    unauth.close();

    PhoneAPITestShim auth;
    bool sawPopulated = false;
    for (const auto &f : pumpConfig(auth, true))
        if (f.which_payload_variant == meshtastic_FromRadio_metadata_tag && f.metadata.firmware_version[0] != '\0')
            sawPopulated = true;
    TEST_ASSERT_TRUE_MESSAGE(sawPopulated, "an authorized client must receive real metadata");
    auth.close();
}

// LoRa is reduced to a public whitelist rather than emptied: region and preset are regulatory
// information a client needs to render a legal picker, so they survive. Everything else must not.
static void test_lora_reduced_to_public_whitelist(void)
{
    ScopedMeshService scopedService;
    ScopedNodeDB scopedNodeDB;
    setLockdownActive(true);
    config.lora = meshtastic_Config_LoRaConfig_init_zero;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.channel_num = 20;
    config.lora.hop_limit = 3;
    config.lora.tx_power = 27;               // not public
    config.lora.override_frequency = 915.5f; // not public
    config.lora.sx126x_rx_boosted_gain = true;

    PhoneAPITestShim unauth;
    meshtastic_Config got = meshtastic_Config_init_zero;
    TEST_ASSERT_TRUE(findConfig(pumpConfig(unauth, false), meshtastic_Config_lora_tag, got));
    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_Config_LoRaConfig_RegionCode_US, got.payload_variant.lora.region,
                              "region is public and must survive");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(20, got.payload_variant.lora.channel_num, "channel_num is in the whitelist");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, (unsigned)got.payload_variant.lora.tx_power, "tx_power is not whitelisted");
    TEST_ASSERT_EQUAL_MESSAGE(0.0f, got.payload_variant.lora.override_frequency, "override_frequency is not whitelisted");
    TEST_ASSERT_FALSE_MESSAGE(got.payload_variant.lora.sx126x_rx_boosted_gain, "gain flag is not whitelisted");
    unauth.close();

    PhoneAPITestShim auth;
    got = meshtastic_Config_init_zero;
    TEST_ASSERT_TRUE(findConfig(pumpConfig(auth, true), meshtastic_Config_lora_tag, got));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(27, (unsigned)got.payload_variant.lora.tx_power,
                                   "an authorized client must receive the full LoRa config");
    auth.close();
}

// MQTT module config carries broker credentials.
static void test_mqtt_module_config_redacted_until_authorized(void)
{
    ScopedMeshService scopedService;
    ScopedNodeDB scopedNodeDB;
    setLockdownActive(true);
    moduleConfig.mqtt = meshtastic_ModuleConfig_MQTTConfig_init_zero;
    strncpy(moduleConfig.mqtt.username, "broker-user", sizeof(moduleConfig.mqtt.username) - 1);
    strncpy(moduleConfig.mqtt.password, "broker-pass", sizeof(moduleConfig.mqtt.password) - 1);
    strncpy(moduleConfig.mqtt.address, "mqtt.example.org", sizeof(moduleConfig.mqtt.address) - 1);

    PhoneAPITestShim unauth;
    bool sawMqtt = false;
    for (const auto &f : pumpConfig(unauth, false))
        if (f.which_payload_variant == meshtastic_FromRadio_moduleConfig_tag &&
            f.moduleConfig.which_payload_variant == meshtastic_ModuleConfig_mqtt_tag) {
            sawMqtt = true;
            TEST_ASSERT_EQUAL_STRING_MESSAGE("", f.moduleConfig.payload_variant.mqtt.password, "broker password must not leak");
            TEST_ASSERT_EQUAL_STRING_MESSAGE("", f.moduleConfig.payload_variant.mqtt.username, "broker username must not leak");
        }
    TEST_ASSERT_TRUE_MESSAGE(sawMqtt, "mqtt module config must still be sent, just emptied");
    unauth.close();

    PhoneAPITestShim auth;
    bool sawCreds = false;
    for (const auto &f : pumpConfig(auth, true))
        if (f.which_payload_variant == meshtastic_FromRadio_moduleConfig_tag &&
            f.moduleConfig.which_payload_variant == meshtastic_ModuleConfig_mqtt_tag &&
            strcmp(f.moduleConfig.payload_variant.mqtt.password, "broker-pass") == 0)
            sawCreds = true;
    TEST_ASSERT_TRUE_MESSAGE(sawCreds, "an authorized client must receive the real MQTT config");
    auth.close();
}

// The node DB is the mesh's social graph. An unauthorized client jumps straight to
// config_complete and receives none of the *other* nodes.
//
// Note what this does NOT assert: the device's own NodeInfo is sent to an unauthorized client
// regardless. STATE_SEND_OWN_NODEINFO has no lockdown gate - only STATE_SEND_OTHER_NODEINFOS
// is skipped - so the node's name, position and User.public_key still go out, even though
// PhoneAPI empties that same public key out of the security config a few frames later. That
// looks like an oversight rather than a decision; it is written up as a finding rather than
// frozen into an assertion here.
static void test_other_nodes_withheld_until_authorized(void)
{
    ScopedMeshService scopedService;
    ScopedNodeDB scopedNodeDB;
    // Populate, or "no other nodes were sent" would be true of an empty DB and prove nothing.
    nodeDB->getOrCreateMeshNode(0x11111111);
    nodeDB->getOrCreateMeshNode(0x22222222);
    setLockdownActive(true);

    PhoneAPITestShim unauth;
    unsigned nodeFrames = 0;
    bool sawComplete = false;
    for (const auto &f : pumpConfig(unauth, false)) {
        if (f.which_payload_variant == meshtastic_FromRadio_node_info_tag)
            nodeFrames++;
        if (f.which_payload_variant == meshtastic_FromRadio_config_complete_id_tag)
            sawComplete = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(sawComplete, "the handshake must still complete");
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(1, nodeFrames, "only our own NodeInfo may reach an unauthorized client");
    unauth.close();

    PhoneAPITestShim auth;
    unsigned authNodeFrames = 0;
    for (const auto &f : pumpConfig(auth, true))
        if (f.which_payload_variant == meshtastic_FromRadio_node_info_tag)
            authNodeFrames++;
    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(1, authNodeFrames, "an authorized client must receive the node DB");
    auth.close();
}
// The inbound direction. An unauthorized connection may deliver lockdown_auth - that is how it
// authenticates - and nothing else. Asserting only the refusal would pass against a build that
// refused everything, including the unlock itself, so both are checked.
//
// Deliberately stops at PhoneAPI's gate: letting a packet through to MeshService would need a
// Router, and routing is not what this asserts.
static void test_unauthorized_client_may_only_send_lockdown_auth(void)
{
    ScopedMeshService scopedService;
    ScopedNodeDB scopedNodeDB;
    setLockdownActive(true);
    // The gate refuses to match while our num is still 0, and MeshService asserts that the
    // local node exists in the DB, so set both.
    myNodeInfo.my_node_num = 0x0BADF00D;
    nodeDB->getOrCreateMeshNode(myNodeInfo.my_node_num);

    PhoneAPITestShim unauth;
    pumpConfig(unauth, false);

    // A text message: refused outright.
    meshtastic_ToRadio text = meshtastic_ToRadio_init_zero;
    text.which_payload_variant = meshtastic_ToRadio_packet_tag;
    text.packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    text.packet.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    text.packet.to = myNodeInfo.my_node_num;
    uint8_t textBytes[meshtastic_ToRadio_size];
    const size_t textLen = pb_encode_to_bytes(textBytes, sizeof(textBytes), &meshtastic_ToRadio_msg, &text);
    TEST_ASSERT_FALSE_MESSAGE(unauth.handleToRadio(textBytes, textLen),
                              "an unauthorized client must not be able to inject mesh traffic");

    // The unlock itself: accepted, handled inline, never routed.
    meshtastic_AdminMessage admin = meshtastic_AdminMessage_init_zero;
    admin.which_payload_variant = meshtastic_AdminMessage_lockdown_auth_tag;
    admin.lockdown_auth.passphrase.size = 4;
    memcpy(admin.lockdown_auth.passphrase.bytes, "pass", 4);

    meshtastic_ToRadio auth = meshtastic_ToRadio_init_zero;
    auth.which_payload_variant = meshtastic_ToRadio_packet_tag;
    auth.packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    auth.packet.decoded.portnum = meshtastic_PortNum_ADMIN_APP;
    auth.packet.to = myNodeInfo.my_node_num;
    auth.packet.decoded.payload.size = pb_encode_to_bytes(
        auth.packet.decoded.payload.bytes, sizeof(auth.packet.decoded.payload.bytes), &meshtastic_AdminMessage_msg, &admin);
    uint8_t authBytes[meshtastic_ToRadio_size];
    const size_t authLen = pb_encode_to_bytes(authBytes, sizeof(authBytes), &meshtastic_ToRadio_msg, &auth);
    TEST_ASSERT_TRUE_MESSAGE(unauth.handleToRadio(authBytes, authLen),
                             "lockdown_auth must reach the device, or a locked node can never be unlocked");
    unauth.close();
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_security_config_redacted_until_authorized);
    RUN_TEST(test_bluetooth_fixed_pin_redacted_until_authorized);
    RUN_TEST(test_network_config_redacted_until_authorized);
    RUN_TEST(test_channels_redacted_until_authorized);
    RUN_TEST(test_device_metadata_wiped_until_authorized);
    RUN_TEST(test_lora_reduced_to_public_whitelist);
    RUN_TEST(test_mqtt_module_config_redacted_until_authorized);
    RUN_TEST(test_other_nodes_withheld_until_authorized);
    RUN_TEST(test_unauthorized_client_may_only_send_lockdown_auth);
    RUN_TEST(test_nothing_is_redacted_while_lockdown_is_inactive);
    exit(UNITY_END());
}

#else // !MESHTASTIC_PHONEAPI_ACCESS_CONTROL

#include "TestUtil.h"
#include <unity.h>

static void test_redaction_gates_absent_from_this_build(void)
{
    TEST_IGNORE_MESSAGE("lockdown gates are compiled out of this env; see [env:coverage-lockdown]");
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_redaction_gates_absent_from_this_build);
    exit(UNITY_END());
}

#endif // MESHTASTIC_PHONEAPI_ACCESS_CONTROL

void loop() {}
