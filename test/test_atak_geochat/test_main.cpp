// On-device text sent as an ATAK GeoChat (TAKPacketV2 on ATAK_PLUGIN_V2) by the TAK roles -
// src/modules/AtakPluginModule.cpp. The envelope must match what the Meshtastic apps put on air
// for an ATAK b-t-f event, because that is what every receiver (Android's TAKPacketV2Conversion,
// the TAKPacket-SDK's CotXmlBuilder) parses: uid GeoChat.<sender>.<chatroom>.<message id>, and
// chat.to unset for All Chat Rooms. device_callsign stays empty: ATAK chats carry no <uid Droid>.
#include "MeshTypes.h" // Include BEFORE TestUtil.h (provides NodeNum, isBroadcast)
#include "TestUtil.h"
#include <unity.h>

#include "mesh/Channels.h"
#include "mesh/CryptoEngine.h"
#include "mesh/NodeDB.h"
#include "mesh/RadioInterface.h"
#include "mesh/mesh-pb-constants.h"
#include "meshUtils.h"
#include "modules/AtakPluginModule.h"
#include <cstring>
#include <memory>
#include <string>

static constexpr NodeNum kLocalNode = 0x0A0A0A0A;
static constexpr NodeNum kPeer = 0x0B0B0B0B;
static constexpr PacketId kPacketId = 0x12345678;

// fillGeoChat() reads nodeDB->getNodeNum(), so a NodeDB must be live.
static NodeDB *testNodeDB = nullptr;

static meshtastic_MeshPacket makeTextPacket(NodeNum to)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.to = to;
    p.id = kPacketId;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    return p;
}

static void setSigningPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy policy)
{
    config.has_security = true;
    config.security.packet_signature_policy = policy;
}

// Switch the primary channel to AES-CCM, as use_aead on a real channel does
static void useAeadPrimaryChannel()
{
    channels.getByIndex(0).settings.use_aead = true;
    channels.onConfigChanged();
    TEST_ASSERT_TRUE_MESSAGE(channels.isAEADEnabled(0), "fixture must leave channel 0 on AES-CCM");
}

// Bytes the packet takes on air once the router has wrapped, signed and encrypted it, as the router
// builds it for a packet we originate: the Data wrapper with bitfield and dest set, a signature when
// signed, the radio header, and the PKC or AEAD overhead when either applies
static size_t frameBytes(const meshtastic_MeshPacket &p, bool isSigned)
{
    meshtastic_Data d = p.decoded;
    d.dest = p.to;
    d.has_bitfield = true;
    d.bitfield = 0x03; // OK_TO_MQTT | WANT_RESPONSE, both bits a sender can set
    d.xeddsa_signature.size = isSigned ? XEDDSA_SIGNATURE_SIZE : 0;
    size_t dataBytes = 0;
    TEST_ASSERT_TRUE(pb_get_encoded_size(&dataBytes, &meshtastic_Data_msg, &d));
    size_t frame = dataBytes + MESHTASTIC_HEADER_LENGTH;
    if (p.pki_encrypted)
        frame += MESHTASTIC_PKC_OVERHEAD;
    else if (channels.isAEADEnabled(p.channel))
        frame += MESHTASTIC_AEAD_OVERHEAD;
    return frame;
}

// Strip the flags byte and decode, as a receiver does. Heap, because the struct is ~2 KB.
static std::unique_ptr<meshtastic_TAKPacketV2> decodeGeoChat(const meshtastic_MeshPacket &p)
{
    TEST_ASSERT_EQUAL(meshtastic_PortNum_ATAK_PLUGIN_V2, p.decoded.portnum);
    TEST_ASSERT_TRUE(p.decoded.payload.size > 1);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFF, p.decoded.payload.bytes[0], "flags byte must mark an uncompressed payload");

    std::unique_ptr<meshtastic_TAKPacketV2> tak(new meshtastic_TAKPacketV2());
    TEST_ASSERT_TRUE(
        pb_decode_from_bytes(p.decoded.payload.bytes + 1, p.decoded.payload.size - 1, &meshtastic_TAKPacketV2_msg, tak.get()));
    TEST_ASSERT_EQUAL(meshtastic_TAKPacketV2_chat_tag, tak->which_payload_variant);
    return tak;
}

void test_broadcast_matches_app_envelope(void)
{
    meshtastic_MeshPacket p = makeTextPacket(NODENUM_BROADCAST);
    const char *text = "hello all";
    TEST_ASSERT_TRUE(AtakPluginModule::setGeoChatPayload(&p, text, strlen(text)));

    auto tak = decodeGeoChat(p);
    TEST_ASSERT_EQUAL(meshtastic_CotType_CotType_b_t_f, tak->cot_type_id);
    TEST_ASSERT_EQUAL(meshtastic_CotHow_CotHow_h_g_i_g_o, tak->how);
    // !<nodenum> is the uid our PLI carries, so ATAK files the chat under the same contact
    TEST_ASSERT_EQUAL_STRING("GeoChat.!0a0a0a0a.All Chat Rooms.12345678", tak->uid);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", tak->device_callsign, "20 bytes of text nothing reads");
    TEST_ASSERT_EQUAL_STRING("Test Operator", tak->callsign);
    TEST_ASSERT_FALSE_MESSAGE(tak->payload_variant.chat.has_to, "All Chat Rooms is signalled by leaving chat.to unset");
    TEST_ASSERT_EQUAL_STRING(text, tak->payload_variant.chat.message);
}

void test_dm_names_recipient_as_chatroom(void)
{
    meshtastic_MeshPacket p = makeTextPacket(kPeer);
    TEST_ASSERT_TRUE(AtakPluginModule::setGeoChatPayload(&p, "just you", 8));

    auto tak = decodeGeoChat(p);
    TEST_ASSERT_TRUE(tak->payload_variant.chat.has_to);
    TEST_ASSERT_EQUAL_STRING("!0b0b0b0b", tak->payload_variant.chat.to);
    TEST_ASSERT_EQUAL_STRING("GeoChat.!0a0a0a0a.!0b0b0b0b.12345678", tak->uid);
    TEST_ASSERT_EQUAL_STRING("just you", tak->payload_variant.chat.message);
}

void test_team_and_role_come_from_tak_config(void)
{
    moduleConfig.has_tak = true;
    moduleConfig.tak.team = meshtastic_Team_Red;
    moduleConfig.tak.role = meshtastic_MemberRole_Medic;

    meshtastic_MeshPacket p = makeTextPacket(NODENUM_BROADCAST);
    TEST_ASSERT_TRUE(AtakPluginModule::setGeoChatPayload(&p, "x", 1));

    auto tak = decodeGeoChat(p);
    TEST_ASSERT_EQUAL(meshtastic_Team_Red, tak->team);
    TEST_ASSERT_EQUAL(meshtastic_MemberRole_Medic, tak->role);
}

void test_unset_team_and_role_use_pli_defaults(void)
{
    meshtastic_MeshPacket p = makeTextPacket(NODENUM_BROADCAST);
    TEST_ASSERT_TRUE(AtakPluginModule::setGeoChatPayload(&p, "x", 1));

    auto tak = decodeGeoChat(p);
    TEST_ASSERT_EQUAL(meshtastic_Team_Cyan, tak->team);
    TEST_ASSERT_EQUAL(meshtastic_MemberRole_TeamMember, tak->role);
}

// The limit the composer shows must be exactly what fits: nothing cut, and the frame within the LoRa
// maximum but not far under it once the router adds what this packet gets (a signature, PKC or AEAD).
// Checked with the longest callsign we send.
static void checkTextAtLimitFits(NodeNum to, bool pki, bool isSigned)
{
    memset(owner.long_name, 'W', MAX_LONG_NAME_BYTES);
    owner.long_name[MAX_LONG_NAME_BYTES] = '\0';

    const size_t limit = AtakPluginModule::maxGeoChatBytes(to, 0, pki);
    TEST_ASSERT_TRUE(limit > 0);
    const std::string text(limit, 'a');

    meshtastic_MeshPacket p = makeTextPacket(to);
    p.pki_encrypted = pki;
    TEST_ASSERT_TRUE(AtakPluginModule::setGeoChatPayload(&p, text.c_str(), text.size()));
    TEST_ASSERT_LESS_OR_EQUAL_size_t(MAX_LORA_PAYLOAD_LEN, frameBytes(p, isSigned));
    // Slack is at most the 2 length bytes the budget reserves for text that ended up shorter
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(MAX_LORA_PAYLOAD_LEN - 2, frameBytes(p, isSigned));

    auto tak = decodeGeoChat(p);
    TEST_ASSERT_EQUAL_size_t(limit, strlen(tak->payload_variant.chat.message));
}

// Compatible never signs, and a plain channel adds nothing to the frame
void test_unsigned_broadcast_text_at_limit_fits(void)
{
    checkTextAtLimitFits(NODENUM_BROADCAST, false, false);
}

// Regression: the budget used to leave no room for the signature the router adds to a broadcast under
// Balanced or Strict, so the router sent a long GeoChat unsigned and Strict receivers dropped it
void test_signed_broadcast_text_at_limit_fits(void)
{
    setSigningPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT);
    checkTextAtLimitFits(NODENUM_BROADCAST, false, true);
}

// AES-CCM's tag authenticates the packet, so the router doesn't sign on an AEAD channel
void test_aead_broadcast_text_at_limit_fits(void)
{
    setSigningPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT);
    useAeadPrimaryChannel();
    checkTextAtLimitFits(NODENUM_BROADCAST, false, false);
}

// PKC authenticates a DM, so it isn't signed either
void test_pki_dm_text_at_limit_fits(void)
{
    setSigningPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_STRICT);
    checkTextAtLimitFits(kPeer, true, false);
}

// The limit is cached for the composer's redraws, so it must follow the callsign and signing policy
void test_limit_follows_callsign_and_policy(void)
{
    const size_t base = AtakPluginModule::maxGeoChatBytes(NODENUM_BROADCAST, 0, false);

    strcpy(owner.long_name, "Test Operator 2");
    TEST_ASSERT_EQUAL_size_t(base - 2, AtakPluginModule::maxGeoChatBytes(NODENUM_BROADCAST, 0, false));

    setSigningPolicy(meshtastic_Config_SecurityConfig_PacketSignaturePolicy_PACKET_SIGNATURE_POLICY_BALANCED);
    TEST_ASSERT_EQUAL_size_t(base - 2 - (XEDDSA_SIGNATURE_SIZE + 2),
                             AtakPluginModule::maxGeoChatBytes(NODENUM_BROADCAST, 0, false));
}

void test_long_text_is_cut_to_limit(void)
{
    const size_t limit = AtakPluginModule::maxGeoChatBytes(NODENUM_BROADCAST, 0, false);
    const std::string text(meshtastic_Constants_DATA_PAYLOAD_LEN, 'a');

    meshtastic_MeshPacket p = makeTextPacket(NODENUM_BROADCAST);
    size_t carried = 0;
    TEST_ASSERT_TRUE(AtakPluginModule::setGeoChatPayload(&p, text.c_str(), text.size(), &carried));
    TEST_ASSERT_LESS_OR_EQUAL_size_t(MAX_LORA_PAYLOAD_LEN, frameBytes(p, false));

    auto tak = decodeGeoChat(p);
    TEST_ASSERT_EQUAL_size_t(limit, strlen(tak->payload_variant.chat.message));
    TEST_ASSERT_EQUAL_size_t_MESSAGE(limit, carried, "carried must report what was sent, so history matches it");
}

// Two-byte characters at an odd limit would leave a dangling lead byte if cut naively
void test_cut_never_splits_utf8(void)
{
    std::string text;
    while (text.size() < meshtastic_Constants_DATA_PAYLOAD_LEN)
        text += "\xC3\xA9"; // é

    for (int nameLen = 1; nameLen <= 2; nameLen++) { // shifts the limit's parity
        memset(owner.long_name, 0, sizeof(owner.long_name));
        memset(owner.long_name, 'W', nameLen);

        meshtastic_MeshPacket p = makeTextPacket(NODENUM_BROADCAST);
        size_t carried = 0;
        TEST_ASSERT_TRUE(AtakPluginModule::setGeoChatPayload(&p, text.c_str(), text.size(), &carried));

        auto tak = decodeGeoChat(p);
        const size_t len = strlen(tak->payload_variant.chat.message);
        TEST_ASSERT_EQUAL_size_t(len, carried);
        TEST_ASSERT_TRUE(len > 0);
        TEST_ASSERT_EQUAL_size_t(0, len % 2);
        TEST_ASSERT_FALSE(sanitizeUtf8(tak->payload_variant.chat.message, sizeof(tak->payload_variant.chat.message)));
    }
}

void test_only_tak_roles_send_geochat(void)
{
    config.device.role = meshtastic_Config_DeviceConfig_Role_TAK;
    TEST_ASSERT_TRUE(AtakPluginModule::sendsTextAsGeoChat());
    config.device.role = meshtastic_Config_DeviceConfig_Role_TAK_TRACKER;
    TEST_ASSERT_TRUE(AtakPluginModule::sendsTextAsGeoChat());
    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    TEST_ASSERT_FALSE(AtakPluginModule::sendsTextAsGeoChat());
    config.device.role = meshtastic_Config_DeviceConfig_Role_TRACKER;
    TEST_ASSERT_FALSE(AtakPluginModule::sendsTextAsGeoChat());
}

void setUp(void)
{
    if (!testNodeDB)
        testNodeDB = new NodeDB(); // its constructor overwrites my_node_num, so claim ours after

    config = meshtastic_LocalConfig_init_zero;
    moduleConfig = meshtastic_LocalModuleConfig_init_zero;
    owner = meshtastic_User_init_zero;
    strcpy(owner.long_name, "Test Operator");
    myNodeInfo.my_node_num = kLocalNode;
    nodeDB = testNodeDB;
    channels.initDefaults();
    channels.getByIndex(0).settings.use_aead = false; // initDefaults() leaves it, so AEAD would leak between tests
    channels.onConfigChanged();
}

void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();

    UNITY_BEGIN();

    RUN_TEST(test_broadcast_matches_app_envelope);
    RUN_TEST(test_dm_names_recipient_as_chatroom);
    RUN_TEST(test_team_and_role_come_from_tak_config);
    RUN_TEST(test_unset_team_and_role_use_pli_defaults);
    RUN_TEST(test_unsigned_broadcast_text_at_limit_fits);
    RUN_TEST(test_signed_broadcast_text_at_limit_fits);
    RUN_TEST(test_aead_broadcast_text_at_limit_fits);
    RUN_TEST(test_pki_dm_text_at_limit_fits);
    RUN_TEST(test_limit_follows_callsign_and_policy);
    RUN_TEST(test_long_text_is_cut_to_limit);
    RUN_TEST(test_cut_never_splits_utf8);
    RUN_TEST(test_only_tak_roles_send_geochat);

    exit(UNITY_END());
}

void loop() {}
