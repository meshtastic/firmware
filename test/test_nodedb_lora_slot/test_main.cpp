// The "heard on the current LoRa config" bit - src/mesh/NodeDB.cpp: loraSlotSnapshotFrom(),
// clearHeardOnCurrentLoraIfSlotChanged(), and the updateFrom() gate that sets it.
#include "MeshTypes.h" // BEFORE TestUtil.h - provides WARM_NODE_COUNT / MAX_NUM_NODES via mesh-pb-constants.h
#include "TestUtil.h"
#include <unity.h>

#if defined(ARCH_PORTDUINO)
#define NDB_TEST_ENTRY extern "C"
#else
#define NDB_TEST_ENTRY
#endif

#include "mesh/NodeDB.h"
#include "mesh/TypeConversions.h"
#include <cstring>

// Name and global scope both fixed by the `friend class NodeDBTestShim` declaration in NodeDB.h.
class NodeDBTestShim : public NodeDB
{
  public:
    void clearHot()
    {
        meshNodes->clear();
        numMeshNodes = 0;
    }

    void pushHeard(NodeNum num, bool favorite = false)
    {
        meshtastic_NodeInfoLite n = meshtastic_NodeInfoLite_init_zero;
        n.num = num;
        n.last_heard = 1000;
        nodeInfoLiteSetBit(&n, NODEINFO_BITFIELD_HEARD_ON_CURRENT_LORA_MASK, true);
        if (favorite)
            nodeInfoLiteSetBit(&n, NODEINFO_BITFIELD_IS_FAVORITE_MASK, true);
        meshNodes->push_back(n);
        numMeshNodes = meshNodes->size();
    }
};

namespace
{

NodeDBTestShim *db = nullptr;
meshtastic_Config_LoRaConfig savedLora;

// Every field the snapshot reads is non-default, so changing one is a real change, not a zero swap.
meshtastic_Config_LoRaConfig baselineLora()
{
    meshtastic_Config_LoRaConfig lora = meshtastic_Config_LoRaConfig_init_zero;
    lora.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
    lora.use_preset = true;
    lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    lora.bandwidth = 250;
    lora.spread_factor = 11;
    lora.coding_rate = 5;
    lora.override_frequency = 869.525f;
    lora.channel_num = 7;
    return lora;
}

bool heard(NodeNum num)
{
    return nodeInfoLiteHeardOnCurrentLora(db->getMeshNode(num));
}

void flipRegion()
{
    config.lora.region = (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_US)
                             ? meshtastic_Config_LoRaConfig_RegionCode_EU_868
                             : meshtastic_Config_LoRaConfig_RegionCode_US;
}

} // namespace

void setUp(void)
{
    db->clearHot();
    config.lora = savedLora;
    db->seedLoraSlotSnapshot();
}

void tearDown(void) {}

// ---------- loraSlotSnapshotFrom: what counts as a slot change ------------------------------

static void test_snapshot_regionChangeIsASlotChange(void)
{
    meshtastic_Config_LoRaConfig other = baselineLora();
    other.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    TEST_ASSERT_TRUE(loraSlotSnapshotFrom(baselineLora(), "LongFast") != loraSlotSnapshotFrom(other, "LongFast"));
}

static void test_snapshot_presetChangeIsASlotChange(void)
{
    meshtastic_Config_LoRaConfig other = baselineLora();
    other.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST;
    TEST_ASSERT_TRUE(loraSlotSnapshotFrom(baselineLora(), "LongFast") != loraSlotSnapshotFrom(other, "LongFast"));
}

static void test_snapshot_channelNumChangeIsASlotChange(void)
{
    meshtastic_Config_LoRaConfig other = baselineLora();
    other.channel_num = 8;
    TEST_ASSERT_TRUE(loraSlotSnapshotFrom(baselineLora(), "LongFast") != loraSlotSnapshotFrom(other, "LongFast"));
}

static void test_snapshot_overrideFrequencyChangeIsASlotChange(void)
{
    meshtastic_Config_LoRaConfig other = baselineLora();
    other.override_frequency = 869.4f;
    TEST_ASSERT_TRUE(loraSlotSnapshotFrom(baselineLora(), "LongFast") != loraSlotSnapshotFrom(other, "LongFast"));
}

// Slot is the hash of the primary channel name, so a rename or a scanned QR moves the radio.
static void test_snapshot_primaryChannelRenameIsASlotChange(void)
{
    const meshtastic_Config_LoRaConfig lora = baselineLora();
    TEST_ASSERT_TRUE(loraSlotSnapshotFrom(lora, "LongFast") != loraSlotSnapshotFrom(lora, "MyMesh"));
}

static void test_snapshot_usePresetToggleIsASlotChange(void)
{
    meshtastic_Config_LoRaConfig other = baselineLora();
    other.use_preset = false;
    TEST_ASSERT_TRUE(loraSlotSnapshotFrom(baselineLora(), "LongFast") != loraSlotSnapshotFrom(other, "LongFast"));
}

// The dormant half of the preset/custom pair moves nothing on air; editing it must not sweep.
static void test_snapshot_dormantModemFieldsIgnoredWhenUsingPreset(void)
{
    meshtastic_Config_LoRaConfig other = baselineLora(); // use_preset = true
    other.bandwidth = 125;
    other.spread_factor = 7;
    other.coding_rate = 8;
    TEST_ASSERT_TRUE(loraSlotSnapshotFrom(baselineLora(), "LongFast") == loraSlotSnapshotFrom(other, "LongFast"));
}

static void test_snapshot_dormantPresetIgnoredWhenNotUsingPreset(void)
{
    meshtastic_Config_LoRaConfig base = baselineLora();
    base.use_preset = false;
    meshtastic_Config_LoRaConfig other = base;
    other.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST;
    TEST_ASSERT_TRUE(loraSlotSnapshotFrom(base, "LongFast") == loraSlotSnapshotFrom(other, "LongFast"));
}

static void test_snapshot_customModemFieldsCountWhenNotUsingPreset(void)
{
    meshtastic_Config_LoRaConfig base = baselineLora();
    base.use_preset = false;
    meshtastic_Config_LoRaConfig bw = base, sf = base, cr = base;
    bw.bandwidth = 125;
    sf.spread_factor = 7;
    cr.coding_rate = 8;
    TEST_ASSERT_TRUE(loraSlotSnapshotFrom(base, "LongFast") != loraSlotSnapshotFrom(bw, "LongFast"));
    TEST_ASSERT_TRUE(loraSlotSnapshotFrom(base, "LongFast") != loraSlotSnapshotFrom(sf, "LongFast"));
    TEST_ASSERT_TRUE(loraSlotSnapshotFrom(base, "LongFast") != loraSlotSnapshotFrom(cr, "LongFast"));
}

// ---------- the sweep -----------------------------------------------------------------------

static void test_clear_noOpWhenSlotUnchanged(void)
{
    db->pushHeard(0x1111);
    db->pushHeard(0x2222);

    TEST_ASSERT_FALSE(db->clearHeardOnCurrentLoraIfSlotChanged());
    TEST_ASSERT_TRUE(heard(0x1111));
    TEST_ASSERT_TRUE(heard(0x2222));
}

static void test_clear_sweepsEveryNodeOnSlotChange(void)
{
    db->pushHeard(0x1111);
    db->pushHeard(0x2222);
    db->pushHeard(0x3333);

    flipRegion();

    TEST_ASSERT_TRUE(db->clearHeardOnCurrentLoraIfSlotChanged());
    TEST_ASSERT_FALSE(heard(0x1111));
    TEST_ASSERT_FALSE(heard(0x2222));
    TEST_ASSERT_FALSE(heard(0x3333));
}

// Self has not been heard on the new config either; leaving it set makes it the one unmarked row.
static void test_clear_sweepsSelf(void)
{
    db->pushHeard(db->getNodeNum()); // index 0 == self
    db->pushHeard(0x1111);

    config.lora.channel_num = config.lora.channel_num + 1;

    TEST_ASSERT_TRUE(db->clearHeardOnCurrentLoraIfSlotChanged());
    TEST_ASSERT_FALSE(heard(db->getNodeNum()));
    TEST_ASSERT_FALSE(heard(0x1111));
}

static void test_clear_leavesOtherBitsAlone(void)
{
    db->pushHeard(0x1111, /*favorite=*/true);

    config.lora.use_preset = !config.lora.use_preset;

    TEST_ASSERT_TRUE(db->clearHeardOnCurrentLoraIfSlotChanged());
    TEST_ASSERT_FALSE(heard(0x1111));
    TEST_ASSERT_TRUE(nodeInfoLiteIsFavorite(db->getMeshNode(0x1111)));
}

// A stuck true would rewrite flash on every save and wipe any bit a hear set in between.
static void test_clear_reSnapshotsSoRepeatIsANoOp(void)
{
    db->pushHeard(0x1111);
    config.lora.channel_num = config.lora.channel_num + 1;

    TEST_ASSERT_TRUE(db->clearHeardOnCurrentLoraIfSlotChanged());
    TEST_ASSERT_FALSE(db->clearHeardOnCurrentLoraIfSlotChanged());

    nodeInfoLiteSetBit(db->getMeshNode(0x1111), NODEINFO_BITFIELD_HEARD_ON_CURRENT_LORA_MASK, true);
    TEST_ASSERT_FALSE(db->clearHeardOnCurrentLoraIfSlotChanged());
    TEST_ASSERT_TRUE(heard(0x1111));
}

// Boot and encrypted-storage unlock baseline through here; it must never be a disguised sweep.
static void test_seed_adoptsConfigWithoutClearing(void)
{
    db->pushHeard(0x1111);
    flipRegion();

    db->seedLoraSlotSnapshot();

    TEST_ASSERT_TRUE(heard(0x1111));
    TEST_ASSERT_FALSE(db->clearHeardOnCurrentLoraIfSlotChanged());
}

// ---------- the setter ----------------------------------------------------------------------

namespace
{
meshtastic_MeshPacket rxPacket(NodeNum from)
{
    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = from;
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    mp.has_rx_time = true;
    mp.rx_time = 1000;
    mp.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    return mp;
}
} // namespace

static void test_set_rfHearSetsTheBit(void)
{
    db->updateFrom(rxPacket(0x4444));
    TEST_ASSERT_TRUE(heard(0x4444));
}

// A gateway rebroadcast is TRANSPORT_LORA + via_mqtt: we heard the gateway, not the node.
static void test_set_mqttRelayDoesNotSetTheBit(void)
{
    meshtastic_MeshPacket mp = rxPacket(0x5555);
    mp.via_mqtt = true;
    db->updateFrom(mp);
    TEST_ASSERT_NOT_NULL(db->getMeshNode(0x5555)); // admitted...
    TEST_ASSERT_FALSE(heard(0x5555));              // ...but not as an RF hear on this config
}

static void test_set_mqttTransportDoesNotSetTheBit(void)
{
    meshtastic_MeshPacket mp = rxPacket(0x6666);
    mp.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_MQTT;
    db->updateFrom(mp);
    TEST_ASSERT_NOT_NULL(db->getMeshNode(0x6666));
    TEST_ASSERT_FALSE(heard(0x6666));
}

// The bit is about the radio, not the clock: an RF hear counts before the clock is trusted.
static void test_set_rfHearCountsWithUntrustedClock(void)
{
    meshtastic_MeshPacket mp = rxPacket(0x7777);
    mp.has_rx_time = false;
    db->updateFrom(mp);
    TEST_ASSERT_TRUE(heard(0x7777));
}

// ---------- wire mirror ---------------------------------------------------------------------

static void test_wire_mirrorsBitToNodeInfo(void)
{
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_zero;
    lite.num = 0x1111;
    TEST_ASSERT_FALSE(TypeConversions::ConvertToNodeInfo(&lite, nullptr, nullptr).heard_on_current_lora);
    nodeInfoLiteSetBit(&lite, NODEINFO_BITFIELD_HEARD_ON_CURRENT_LORA_MASK, true);
    TEST_ASSERT_TRUE(TypeConversions::ConvertToNodeInfo(&lite, nullptr, nullptr).heard_on_current_lora);
}

NDB_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    db = new NodeDBTestShim();
    nodeDB = db;
    savedLora = config.lora;

    UNITY_BEGIN();
    RUN_TEST(test_snapshot_regionChangeIsASlotChange);
    RUN_TEST(test_snapshot_presetChangeIsASlotChange);
    RUN_TEST(test_snapshot_channelNumChangeIsASlotChange);
    RUN_TEST(test_snapshot_overrideFrequencyChangeIsASlotChange);
    RUN_TEST(test_snapshot_primaryChannelRenameIsASlotChange);
    RUN_TEST(test_snapshot_usePresetToggleIsASlotChange);
    RUN_TEST(test_snapshot_dormantModemFieldsIgnoredWhenUsingPreset);
    RUN_TEST(test_snapshot_dormantPresetIgnoredWhenNotUsingPreset);
    RUN_TEST(test_snapshot_customModemFieldsCountWhenNotUsingPreset);
    RUN_TEST(test_clear_noOpWhenSlotUnchanged);
    RUN_TEST(test_clear_sweepsEveryNodeOnSlotChange);
    RUN_TEST(test_clear_sweepsSelf);
    RUN_TEST(test_clear_leavesOtherBitsAlone);
    RUN_TEST(test_clear_reSnapshotsSoRepeatIsANoOp);
    RUN_TEST(test_seed_adoptsConfigWithoutClearing);
    RUN_TEST(test_set_rfHearSetsTheBit);
    RUN_TEST(test_set_mqttRelayDoesNotSetTheBit);
    RUN_TEST(test_set_mqttTransportDoesNotSetTheBit);
    RUN_TEST(test_set_rfHearCountsWithUntrustedClock);
    RUN_TEST(test_wire_mirrorsBitToNodeInfo);
    exit(UNITY_END());
}
NDB_TEST_ENTRY void loop() {}
