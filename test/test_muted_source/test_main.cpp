// isMutedForPacket() source resolution - src/mesh/Channels.cpp. A DM addressed to us reads the
// sender's mute bit; every other packet reads the mute bit of the channel it arrived on.
#include "MeshTypes.h" // Include BEFORE TestUtil.h (provides NodeNum, isToUs, isBroadcast)
#include "TestUtil.h"
#include <unity.h>

#include "mesh/Channels.h"
#include "mesh/MeshService.h"
#include "mesh/NodeDB.h"
#include <cstdio>
#include <cstring>

static constexpr NodeNum kLocalNode = 0x11111111;
static constexpr NodeNum kPeer = 0x22222222;
static constexpr NodeNum kThirdParty = 0x33333333;
static constexpr NodeNum kStranger = 0x44444444; // deliberately never added to the DB

// isToUs() reads nodeDB->getNodeNum() and the DM branch looks the sender up, so a real NodeDB
// must be live.
static NodeDB *testNodeDB = nullptr;

static meshtastic_MeshPacket makePacket(NodeNum from, NodeNum to, uint8_t channel)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.from = from;
    p.to = to;
    p.channel = channel;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    return p;
}

static void setSlot(ChannelIndex idx, meshtastic_Channel_Role role, bool muted)
{
    meshtastic_Channel &ch = channels.getByIndex(idx);
    ch.index = idx;
    ch.has_settings = true;
    ch.role = role;
    ch.settings.has_module_settings = true;
    ch.settings.module_settings.is_muted = muted;
}

// Append straight into the hot store: getOrCreateMeshNode() would drag in the cap and
// eviction machinery, which this predicate has nothing to do with.
static void setNodeMuted(NodeNum num, bool muted)
{
    meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(num);
    if (!n) {
        nodeDB->meshNodes->resize(nodeDB->numMeshNodes + 1);
        n = &nodeDB->meshNodes->at(nodeDB->numMeshNodes++);
        memset(n, 0, sizeof(*n));
        n->num = num;
    }
    nodeInfoLiteSetBit(n, NODEINFO_BITFIELD_IS_MUTED_MASK, muted);
}

// ---------------------------------------------------------------------------
// Broadcast: the arrival channel decides
// ---------------------------------------------------------------------------

void test_broadcast_on_unmuted_channel_is_not_muted()
{
    TEST_ASSERT_FALSE(isMutedForPacket(makePacket(kPeer, NODENUM_BROADCAST, 0)));
}

void test_broadcast_on_muted_channel()
{
    setSlot(0, meshtastic_Channel_Role_PRIMARY, true);
    TEST_ASSERT_TRUE(isMutedForPacket(makePacket(kPeer, NODENUM_BROADCAST, 0)));
}

// A channel with no module_settings at all has never been muted.
void test_channel_without_module_settings_is_not_muted()
{
    meshtastic_Channel &ch = channels.getByIndex(0);
    ch.settings.has_module_settings = false;
    ch.settings.module_settings.is_muted = true; // stale payload behind the presence flag
    TEST_ASSERT_FALSE(isMutedForPacket(makePacket(kPeer, NODENUM_BROADCAST, 0)));
}

// Mute is per channel, not global: a muted secondary must not silence the others.
void test_broadcast_reads_its_own_channel()
{
    setSlot(2, meshtastic_Channel_Role_SECONDARY, true);
    setSlot(1, meshtastic_Channel_Role_SECONDARY, false);
    TEST_ASSERT_TRUE(isMutedForPacket(makePacket(kPeer, NODENUM_BROADCAST, 2)));
    TEST_ASSERT_FALSE(isMutedForPacket(makePacket(kPeer, NODENUM_BROADCAST, 1)));
}

// channel == 0 means "the primary", which is not always slot 0.
void test_channel_zero_resolves_to_primary_slot()
{
    setSlot(0, meshtastic_Channel_Role_SECONDARY, false);
    setSlot(3, meshtastic_Channel_Role_PRIMARY, true);
    channels.onConfigChanged();
    TEST_ASSERT_EQUAL_UINT8(3, channels.getPrimaryIndex());
    TEST_ASSERT_TRUE(isMutedForPacket(makePacket(kPeer, NODENUM_BROADCAST, 0)));
}

// ---------------------------------------------------------------------------
// DM addressed to us: the sender decides
// ---------------------------------------------------------------------------

void test_dm_to_us_from_muted_sender()
{
    setNodeMuted(kPeer, true);
    TEST_ASSERT_TRUE(isMutedForPacket(makePacket(kPeer, kLocalNode, 0)));
}

void test_dm_to_us_from_unmuted_sender_is_not_muted()
{
    setNodeMuted(kPeer, false);
    TEST_ASSERT_FALSE(isMutedForPacket(makePacket(kPeer, kLocalNode, 0)));
}

// The discriminator: a DM must not inherit its channel's mute state.
void test_dm_to_us_ignores_channel_mute()
{
    setSlot(0, meshtastic_Channel_Role_PRIMARY, true);
    setNodeMuted(kPeer, false);
    TEST_ASSERT_FALSE(isMutedForPacket(makePacket(kPeer, kLocalNode, 0)));
}

// A sender we have never heard of has no mute bit to read.
void test_dm_to_us_from_unknown_sender_is_not_muted()
{
    TEST_ASSERT_FALSE(isMutedForPacket(makePacket(kStranger, kLocalNode, 0)));
}

// Not addressed to us: overheard traffic falls back to the channel, sender mute is irrelevant.
void test_dm_to_third_party_uses_channel()
{
    setSlot(0, meshtastic_Channel_Role_PRIMARY, true);
    setNodeMuted(kPeer, false);
    TEST_ASSERT_TRUE(isMutedForPacket(makePacket(kPeer, kThirdParty, 0)));

    setSlot(0, meshtastic_Channel_Role_PRIMARY, false);
    setNodeMuted(kPeer, true);
    TEST_ASSERT_FALSE(isMutedForPacket(makePacket(kPeer, kThirdParty, 0)));
}

// ---------------------------------------------------------------------------
// Alert payloads, which break through a mute
// ---------------------------------------------------------------------------

// ASCII BEL, the in-band alert marker. Numeric so no control byte sits in the source.
static const uint8_t kAsciiBell = 7;

static meshtastic_MeshPacket withText(meshtastic_MeshPacket p, const char *text, bool bell)
{
    p.decoded.payload.size = (pb_size_t)strlen(text);
    memcpy(p.decoded.payload.bytes, text, p.decoded.payload.size);
    if (bell)
        p.decoded.payload.bytes[p.decoded.payload.size++] = kAsciiBell;
    return p;
}

void test_bell_is_an_alert_when_a_bell_output_is_on()
{
    moduleConfig.external_notification.alert_bell = true;
    TEST_ASSERT_TRUE(MeshService::isAlertPayload(withText(makePacket(kPeer, NODENUM_BROADCAST, 0), "wake up", true)));
}

void test_bell_is_not_an_alert_when_every_bell_output_is_off()
{
    TEST_ASSERT_FALSE(MeshService::isAlertPayload(withText(makePacket(kPeer, NODENUM_BROADCAST, 0), "wake up", true)));
}

void test_plain_text_is_never_an_alert()
{
    moduleConfig.external_notification.alert_bell = true;
    TEST_ASSERT_FALSE(MeshService::isAlertPayload(withText(makePacket(kPeer, NODENUM_BROADCAST, 0), "wake up", false)));
}

// The wake gate is "not muted, or an alert": a bell must survive a muted channel.
void test_alert_survives_a_muted_channel()
{
    moduleConfig.external_notification.alert_bell = true;
    setSlot(0, meshtastic_Channel_Role_PRIMARY, true);
    const meshtastic_MeshPacket p = withText(makePacket(kPeer, NODENUM_BROADCAST, 0), "wake up", true);
    TEST_ASSERT_TRUE(isMutedForPacket(p));
    TEST_ASSERT_TRUE(!isMutedForPacket(p) || MeshService::isAlertPayload(p));
}

// ---------------------------------------------------------------------------
// Unity lifecycle
// ---------------------------------------------------------------------------

void setUp(void)
{
    if (!testNodeDB)
        testNodeDB = new NodeDB(); // its constructor overwrites my_node_num, so claim ours after

    config = meshtastic_LocalConfig_init_zero;
    moduleConfig = meshtastic_LocalModuleConfig_init_zero;
    myNodeInfo.my_node_num = kLocalNode;
    nodeDB = testNodeDB;

    // Start from an empty hot store so kStranger is genuinely unknown.
    nodeDB->meshNodes->clear();
    nodeDB->numMeshNodes = 0;

    memset(&channelFile, 0, sizeof(channelFile));
    channels.initDefaults();
    channels.onConfigChanged();
}

void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();

    UNITY_BEGIN();

    printf("\n=== Broadcast: channel mute ===\n");
    RUN_TEST(test_broadcast_on_unmuted_channel_is_not_muted);
    RUN_TEST(test_broadcast_on_muted_channel);
    RUN_TEST(test_channel_without_module_settings_is_not_muted);
    RUN_TEST(test_broadcast_reads_its_own_channel);
    RUN_TEST(test_channel_zero_resolves_to_primary_slot);

    printf("\n=== Direct message: sender mute ===\n");
    RUN_TEST(test_dm_to_us_from_muted_sender);
    RUN_TEST(test_dm_to_us_from_unmuted_sender_is_not_muted);
    RUN_TEST(test_dm_to_us_ignores_channel_mute);
    RUN_TEST(test_dm_to_us_from_unknown_sender_is_not_muted);
    RUN_TEST(test_dm_to_third_party_uses_channel);

    printf("\n=== Alerts break through mute ===\n");
    RUN_TEST(test_bell_is_an_alert_when_a_bell_output_is_on);
    RUN_TEST(test_bell_is_not_an_alert_when_every_bell_output_is_off);
    RUN_TEST(test_plain_text_is_never_an_alert);
    RUN_TEST(test_alert_survives_a_muted_channel);

    exit(UNITY_END());
}

void loop() {}
