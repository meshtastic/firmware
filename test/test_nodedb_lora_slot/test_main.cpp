// The "heard on the current LoRa config" mark - src/mesh/NodeDB.cpp and src/mesh/TypeConversions.cpp.
// Each node stores the slot it was last heard on; NodeInfo.heard_on_current_lora is that matching the
// slot the radio is committed to. The regression guarded is a client rolling through presets to scan
// for traffic: nothing may be swept on the way out, and returning to a slot must mark its nodes again.
#include "MeshTypes.h" // BEFORE TestUtil.h - provides WARM_NODE_COUNT / MAX_NUM_NODES via mesh-pb-constants.h
#include "TestUtil.h"
#include <unity.h>

#if defined(ARCH_PORTDUINO)
#define NDB_TEST_ENTRY extern "C"
#else
#define NDB_TEST_ENTRY
#endif

#include "mesh/Channels.h"
#include "mesh/NodeDB.h"
#include "mesh/RadioInterface.h"
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

    // A node admitted without ever being heard over RF - an all-zero bitfield, as a pre-feature
    // record loaded from disk has.
    void push(NodeNum num)
    {
        meshtastic_NodeInfoLite n = meshtastic_NodeInfoLite_init_zero;
        n.num = num;
        n.last_heard = 1000;
        meshNodes->push_back(n);
        numMeshNodes = meshNodes->size();
    }
};

namespace
{

NodeDBTestShim *db = nullptr;
meshtastic_Config_LoRaConfig savedLora;
meshtastic_ChannelSettings savedPrimary; // a borrow rewrites the primary in place

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

uint16_t fp(const meshtastic_Config_LoRaConfig &lora, const char *name)
{
    return loraSlotSnapshotFrom(lora, name).fingerprint();
}

// What a client actually sees: derived at conversion time from the slot the radio is committed to.
bool heard(NodeNum num)
{
    return TypeConversions::ConvertToNodeInfo(db->getMeshNode(num), nullptr, nullptr).heard_on_current_lora;
}

// A decoded packet as updateFrom() sees it coming off the RX pipeline.
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

// Move the radio the way a client's set_config(lora) does, committing to the new slot.
void commitPreset(meshtastic_Config_LoRaConfig_ModemPreset preset)
{
    config.lora.modem_preset = preset;
    db->refreshCommittedLoraSlot();
}

void commitHome()
{
    config.lora = savedLora;
    db->refreshCommittedLoraSlot();
}

} // namespace

void setUp(void)
{
    db->clearHot();
    config.lora = savedLora;
    channels.getByIndex(channels.getPrimaryIndex()).settings = savedPrimary;
    db->setLoraSlotTransient(false);
    db->refreshCommittedLoraSlot();
}

void tearDown(void) {}

// ---------- the fingerprint: what counts as a different slot ---------------------------------

static void test_fingerprint_identicalConfigMatches(void)
{
    const meshtastic_Config_LoRaConfig lora = baselineLora();
    TEST_ASSERT_EQUAL_UINT16(fp(lora, "LongFast"), fp(lora, "LongFast"));
}

static void test_fingerprint_regionIsASlotChange(void)
{
    meshtastic_Config_LoRaConfig other = baselineLora();
    other.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    TEST_ASSERT_NOT_EQUAL_UINT16(fp(baselineLora(), "LongFast"), fp(other, "LongFast"));
}

static void test_fingerprint_presetIsASlotChange(void)
{
    meshtastic_Config_LoRaConfig other = baselineLora();
    other.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST;
    TEST_ASSERT_NOT_EQUAL_UINT16(fp(baselineLora(), "LongFast"), fp(other, "LongFast"));
}

static void test_fingerprint_channelNumChangesSlot(void)
{
    meshtastic_Config_LoRaConfig other = baselineLora();
    other.channel_num = 8;
    TEST_ASSERT_NOT_EQUAL_UINT16(fp(baselineLora(), "LongFast"), fp(other, "LongFast"));
}

static void test_fingerprint_overrideFrequencyIsASlotChange(void)
{
    meshtastic_Config_LoRaConfig other = baselineLora();
    other.override_frequency = 869.4f;
    TEST_ASSERT_NOT_EQUAL_UINT16(fp(baselineLora(), "LongFast"), fp(other, "LongFast"));
}

// Slot is the hash of the primary channel name, so a rename or a scanned QR moves the radio.
static void test_fingerprint_primaryChannelRenameIsASlotChange(void)
{
    const meshtastic_Config_LoRaConfig lora = baselineLora();
    TEST_ASSERT_NOT_EQUAL_UINT16(fp(lora, "LongFast"), fp(lora, "MyMesh"));
}

static void test_fingerprint_usePresetToggleIsASlotChange(void)
{
    meshtastic_Config_LoRaConfig other = baselineLora();
    other.use_preset = false;
    TEST_ASSERT_NOT_EQUAL_UINT16(fp(baselineLora(), "LongFast"), fp(other, "LongFast"));
}

// The dormant half of the preset/custom pair moves nothing on air; editing it must not read as a move.
static void test_fingerprint_dormantModemFieldsIgnoredWhenUsingPreset(void)
{
    meshtastic_Config_LoRaConfig other = baselineLora(); // use_preset = true
    other.bandwidth = 125;
    other.spread_factor = 7;
    other.coding_rate = 8;
    TEST_ASSERT_EQUAL_UINT16(fp(baselineLora(), "LongFast"), fp(other, "LongFast"));
}

static void test_fingerprint_dormantPresetIgnoredWhenNotUsingPreset(void)
{
    meshtastic_Config_LoRaConfig base = baselineLora();
    base.use_preset = false;
    meshtastic_Config_LoRaConfig other = base;
    other.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST;
    TEST_ASSERT_EQUAL_UINT16(fp(base, "LongFast"), fp(other, "LongFast"));
}

static void test_fingerprint_customModemFieldsCountWhenNotUsingPreset(void)
{
    meshtastic_Config_LoRaConfig base = baselineLora();
    base.use_preset = false;
    meshtastic_Config_LoRaConfig bw = base, sf = base, cr = base;
    bw.bandwidth = 125;
    sf.spread_factor = 7;
    cr.coding_rate = 8;
    TEST_ASSERT_NOT_EQUAL_UINT16(fp(base, "LongFast"), fp(bw, "LongFast"));
    TEST_ASSERT_NOT_EQUAL_UINT16(fp(base, "LongFast"), fp(sf, "LongFast"));
    TEST_ASSERT_NOT_EQUAL_UINT16(fp(base, "LongFast"), fp(cr, "LongFast"));
}

// ---------- storing the slot on a hear -------------------------------------------------------

static void test_hear_rfHearMarksNodeOnCurrentSlot(void)
{
    db->updateFrom(rxPacket(0x4444));
    TEST_ASSERT_TRUE(heard(0x4444));
}

// A gateway rebroadcast is TRANSPORT_LORA + via_mqtt: we heard the gateway, not the node.
static void test_hear_mqttRelayDoesNotMark(void)
{
    meshtastic_MeshPacket mp = rxPacket(0x5555);
    mp.via_mqtt = true;
    db->updateFrom(mp);
    TEST_ASSERT_NOT_NULL(db->getMeshNode(0x5555)); // admitted...
    TEST_ASSERT_FALSE(heard(0x5555));              // ...but not as an RF hear on this slot
}

static void test_hear_mqttTransportDoesNotMark(void)
{
    meshtastic_MeshPacket mp = rxPacket(0x6666);
    mp.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_MQTT;
    db->updateFrom(mp);
    TEST_ASSERT_NOT_NULL(db->getMeshNode(0x6666));
    TEST_ASSERT_FALSE(heard(0x6666));
}

// The mark is about the radio, not the clock: an RF hear counts before the clock is trusted.
static void test_hear_countsWithUntrustedClock(void)
{
    meshtastic_MeshPacket mp = rxPacket(0x7777);
    mp.has_rx_time = false;
    db->updateFrom(mp);
    TEST_ASSERT_TRUE(heard(0x7777));
}

// A pre-feature record has an all-zero bitfield. Without the has-RF-hear bit gating it, stored slot 0
// would collide with whatever the radio happens to be on and mark every legacy node heard.
static void test_hear_legacyRecordReadsUnheard(void)
{
    db->push(0xAAAA);
    TEST_ASSERT_FALSE(heard(0xAAAA));
}

// ---------- the scan: rolling through presets and back ---------------------------------------

// The regression this design exists for. A client scanning A->B->C->A must leave A's marks intact:
// the hops sweep nothing, and coming home makes the stored slots match again on their own.
static void test_scan_roundTripRestoresTheMark(void)
{
    db->updateFrom(rxPacket(0x1111)); // heard on A
    TEST_ASSERT_TRUE(heard(0x1111));

    commitPreset(meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST); // hop to B
    TEST_ASSERT_FALSE(heard(0x1111));                                  // unreachable while parked on B

    commitPreset(meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST); // hop to C
    TEST_ASSERT_FALSE(heard(0x1111));

    commitHome();
    TEST_ASSERT_TRUE(heard(0x1111));
}

// The other direction: a node heard only while parked on B must not read as reachable back on A.
static void test_scan_nodeHeardOnOtherSlotStaysUnheardAtHome(void)
{
    commitPreset(meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST);
    db->updateFrom(rxPacket(0x2222)); // a foreign node, heard on B
    TEST_ASSERT_TRUE(heard(0x2222));

    commitHome();
    TEST_ASSERT_FALSE(heard(0x2222));
}

// Re-reading the committed slot is not a sweep: it must never touch a node's stored bitfield, which
// is what keeps a scan off the flash and makes the round trip above possible at all.
static void test_scan_refreshWritesNoNode(void)
{
    db->updateFrom(rxPacket(0x3333));
    const uint32_t before = db->getMeshNode(0x3333)->bitfield;

    commitPreset(meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST);

    TEST_ASSERT_EQUAL_UINT32(before, db->getMeshNode(0x3333)->bitfield);
}

// ---------- transient switch (a beacon keyed up on another preset) ---------------------------

// MeshBeaconModule rewrites config.lora for a beacon TX and restores it. The committed slot is pinned
// across that window, so the whole node list does not blink to unheard while we key up elsewhere.
static void test_transient_committedSlotIsPinned(void)
{
    db->updateFrom(rxPacket(0x1111));
    const uint16_t home = db->committedLoraSlot();

    db->setLoraSlotTransient(true);
    commitPreset(meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST);

    TEST_ASSERT_EQUAL_UINT16(home, db->committedLoraSlot());
    TEST_ASSERT_TRUE(heard(0x1111));
}

// A hear while parked on the beacon's preset belongs to that preset, so it stops matching at home.
static void test_transient_hearIsStampedWithTheLiveSlot(void)
{
    db->setLoraSlotTransient(true);
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST;
    db->updateFrom(rxPacket(0x8888));

    db->setLoraSlotTransient(false);
    commitHome();

    TEST_ASSERT_NOT_NULL(db->getMeshNode(0x8888)); // still admitted
    TEST_ASSERT_FALSE(heard(0x8888));
}

// ---------- a borrow is never reported or persisted as this node's own config -----------------

// What MeshBeaconModule does for a beacon TX: mark the slot transient, then rewrite the RF identity and
// the primary channel in place. Each value differs from home, so reading the wrong one is visible.
static const char kVisitedName[] = "Visited";

// A stand-in driver: its reconfigure() is the base one, so applyModemConfig() runs exactly as on a device.
class BorrowingRadio : public RadioInterface
{
  public:
    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        packetPool.release(p);
        return ERRNO_OK;
    }
    uint32_t getPacketTime(uint32_t, bool = false) override { return 0; }
};

static void borrowRadio(RadioInterface &radio)
{
    db->setLoraSlotTransient(true);
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST;
    config.lora.use_preset = true;
    config.lora.channel_num = 3;
    meshtastic_ChannelSettings &primary = channels.getByIndex(channels.getPrimaryIndex()).settings;
    strncpy(primary.name, kVisitedName, sizeof(primary.name) - 1);
    primary.psk.size = 16;
    memset(primary.psk.bytes, 0x77, 16);
    radio.reconfigure(); // the beacon programs the borrow through here, never through commitConfig()
}

/*
 * Under test: RadioInterface::captureConfiguredRadio() while NodeDB's LoRa slot is transient
 * (src/mesh/RadioInterface.cpp).
 * Why: any config or channel save runs MeshService::reloadConfig() -> commitConfig() -> this capture. During a
 * beacon switch config.lora and the primary hold the visited mesh, so the snapshot every status gate reads
 * would answer for that mesh until the next commit.
 * Regression guarded: a save landing mid-switch committing the beacon's region, preset, slot and channel as
 * the node's own - while still adopting a non-RF field the operator really did change.
 */
static void test_borrow_commitKeepsTheCommittedRadio(void)
{
    RadioInterface::uses_default_frequency_slot = true;
    RadioInterface::captureConfiguredRadio(); // home, committed
    const meshtastic_Config_LoRaConfig home = config.lora;
    const meshtastic_ChannelSettings homePrimary = channels.getByIndex(channels.getPrimaryIndex()).settings;

    BorrowingRadio radio;
    borrowRadio(radio);
    RadioInterface::uses_default_frequency_slot = false; // the switch republishes the live verdict
    config.lora.tx_power = home.tx_power + 3;            // a non-RF field a commit must still adopt
    RadioInterface::captureConfiguredRadio();            // a save mid-switch

    const meshtastic_Config_LoRaConfig &committed = RadioInterface::configuredLoraConfig();
    TEST_ASSERT_EQUAL_MESSAGE(home.region, committed.region, "the borrowed region is not committed");
    TEST_ASSERT_EQUAL_MESSAGE(home.modem_preset, committed.modem_preset, "nor the borrowed preset");
    TEST_ASSERT_EQUAL_MESSAGE(home.use_preset, committed.use_preset, "nor use_preset");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(home.channel_num, committed.channel_num, "nor the borrowed slot");
    TEST_ASSERT_TRUE_MESSAGE(RadioInterface::configuredUsesDefaultSlot(), "nor the borrowed slot verdict");
    TEST_ASSERT_EQUAL_INT_MESSAGE(home.tx_power + 3, committed.tx_power, "but the rest of the commit is adopted");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(homePrimary.name, channels.getChannelToReport(channels.getPrimaryIndex()).settings.name,
                                     "and the committed primary is the home channel, not the visited one");

    db->setLoraSlotTransient(false);
    RadioInterface::captureConfiguredRadio(); // the next commit outside a borrow adopts the live radio again
    TEST_ASSERT_EQUAL_MESSAGE(config.lora.modem_preset, RadioInterface::configuredLoraConfig().modem_preset,
                              "a commit outside a borrow is a real commit");
}

/*
 * Under test: NodeDB::saveToDiskNoRetry() (SEGMENT_CONFIG) and saveChannelsToDisk() while borrowed
 * (src/mesh/NodeDB.cpp).
 * Why: flash is what the node boots onto. A save mid-switch wrote config.lora and the channel table verbatim,
 * so the node came back up on the beacon's mesh with the beacon's channel as its primary.
 * Regression guarded: the borrow persisted; and, the other way, the save leaving the live (borrowed) radio
 * rewritten - the beacon is still on the air and its restore expects its own values back.
 */
static void test_borrow_saveWritesTheCommittedRadio(void)
{
    RadioInterface::captureConfiguredRadio();
    const meshtastic_Config_LoRaConfig home = config.lora;
    const meshtastic_ChannelSettings homePrimary = channels.getByIndex(channels.getPrimaryIndex()).settings;

    BorrowingRadio radio;
    borrowRadio(radio);
    TEST_ASSERT_TRUE(db->saveToDisk(SEGMENT_CONFIG | SEGMENT_CHANNELS));

    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST, config.lora.modem_preset,
                              "the save hands the borrowed radio back untouched");
    TEST_ASSERT_EQUAL_STRING(kVisitedName, channels.getByIndex(channels.getPrimaryIndex()).settings.name);

    meshtastic_LocalConfig onDisk = meshtastic_LocalConfig_init_zero;
    TEST_ASSERT_EQUAL(LoadFileResult::LOAD_SUCCESS, db->loadProto(configFileName, meshtastic_LocalConfig_size, sizeof(onDisk),
                                                                  &meshtastic_LocalConfig_msg, &onDisk));
    TEST_ASSERT_EQUAL_MESSAGE(home.region, onDisk.lora.region, "flash holds the committed region");
    TEST_ASSERT_EQUAL_MESSAGE(home.modem_preset, onDisk.lora.modem_preset, "and preset");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(home.channel_num, onDisk.lora.channel_num, "and slot");

    meshtastic_ChannelFile channelsOnDisk = meshtastic_ChannelFile_init_zero;
    TEST_ASSERT_EQUAL(LoadFileResult::LOAD_SUCCESS,
                      db->loadProto(channelFileName, meshtastic_ChannelFile_size, sizeof(channelsOnDisk),
                                    &meshtastic_ChannelFile_msg, &channelsOnDisk));
    TEST_ASSERT_EQUAL_STRING_MESSAGE(homePrimary.name, channelsOnDisk.channels[channels.getPrimaryIndex()].settings.name,
                                     "and the committed primary, not the visited channel");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(homePrimary.psk.size, channelsOnDisk.channels[channels.getPrimaryIndex()].settings.psk.size,
                                   "with its own key");
}

/*
 * Under test: RadioInterface::loraConfigToReport() and Channels::getChannelToReport() with no borrow.
 * Why: inside an open edit transaction set_config/set_channel change RAM and defer the commit, and a client
 * reading back mid-transaction must see its own edits. Only a borrow substitutes the committed values.
 * Regression guarded: the report reading the snapshot unconditionally, hiding every uncommitted edit.
 */
static void test_noBorrow_reportsTheLiveRadio(void)
{
    RadioInterface::captureConfiguredRadio();
    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW; // edited, not yet committed
    meshtastic_ChannelSettings &primary = channels.getByIndex(channels.getPrimaryIndex()).settings;
    strncpy(primary.name, "Edited", sizeof(primary.name) - 1);

    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW, RadioInterface::loraConfigToReport().modem_preset);
    TEST_ASSERT_EQUAL_STRING("Edited", channels.getChannelToReport(channels.getPrimaryIndex()).settings.name);
}

/*
 * Under test: captureConfiguredRadio() and loraConfigToReport() when the RF identity moves mid-borrow by
 * something other than the borrow (src/mesh/RadioInterface.cpp, the borrowedRf record).
 * Why: an operator's edit - set_config(lora), or the on-device preset and region menus, which write config.lora
 * directly - can land while a beacon holds the radio. It is the newer instruction, so a client must read it
 * back and the commit must take it, rather than the snapshot from before the borrow.
 * Regression guarded: the borrow guard keeping the old snapshot over the operator's edit, so the commit and
 * the save dropped it.
 */
static void test_borrow_operatorEditMidBorrowIsCommitted(void)
{
    RadioInterface::captureConfiguredRadio(); // home, committed
    BorrowingRadio radio;
    borrowRadio(radio);

    config.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW; // what a menu or admin write does
    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW,
                              RadioInterface::loraConfigToReport().modem_preset, "a client reads back the edit");

    radio.commitConfig(); // reloadConfig(), still mid-borrow
    TEST_ASSERT_EQUAL_MESSAGE(meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW,
                              RadioInterface::configuredLoraConfig().modem_preset, "and the commit takes it as the node's own");
}

NDB_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    db = new NodeDBTestShim();
    nodeDB = db;
    savedLora = config.lora;
    savedPrimary = channels.getByIndex(channels.getPrimaryIndex()).settings;

    UNITY_BEGIN();
    RUN_TEST(test_fingerprint_identicalConfigMatches);
    RUN_TEST(test_fingerprint_regionIsASlotChange);
    RUN_TEST(test_fingerprint_presetIsASlotChange);
    RUN_TEST(test_fingerprint_channelNumChangesSlot);
    RUN_TEST(test_fingerprint_overrideFrequencyIsASlotChange);
    RUN_TEST(test_fingerprint_primaryChannelRenameIsASlotChange);
    RUN_TEST(test_fingerprint_usePresetToggleIsASlotChange);
    RUN_TEST(test_fingerprint_dormantModemFieldsIgnoredWhenUsingPreset);
    RUN_TEST(test_fingerprint_dormantPresetIgnoredWhenNotUsingPreset);
    RUN_TEST(test_fingerprint_customModemFieldsCountWhenNotUsingPreset);
    RUN_TEST(test_hear_rfHearMarksNodeOnCurrentSlot);
    RUN_TEST(test_hear_mqttRelayDoesNotMark);
    RUN_TEST(test_hear_mqttTransportDoesNotMark);
    RUN_TEST(test_hear_countsWithUntrustedClock);
    RUN_TEST(test_hear_legacyRecordReadsUnheard);
    RUN_TEST(test_scan_roundTripRestoresTheMark);
    RUN_TEST(test_scan_nodeHeardOnOtherSlotStaysUnheardAtHome);
    RUN_TEST(test_scan_refreshWritesNoNode);
    RUN_TEST(test_transient_committedSlotIsPinned);
    RUN_TEST(test_transient_hearIsStampedWithTheLiveSlot);
    RUN_TEST(test_borrow_commitKeepsTheCommittedRadio);
    RUN_TEST(test_borrow_saveWritesTheCommittedRadio);
    RUN_TEST(test_noBorrow_reportsTheLiveRadio);
    RUN_TEST(test_borrow_operatorEditMidBorrowIsCommitted);
    exit(UNITY_END());
}
NDB_TEST_ENTRY void loop() {}
