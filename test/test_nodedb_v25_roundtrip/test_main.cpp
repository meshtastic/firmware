// Round-trip fidelity of the v25 slim NodeDB persistence cycle: snr_q4 quantization and its
// HAS_SNR sentinel, satellite-map projection/rehydration and eviction, the keyless-device write
// skip, and resetNodes() compaction. Each test saves, cold-boots a real NodeDB, and reads back.
#include "MeshTypes.h" // BEFORE TestUtil.h - provides MAX_SATELLITE_NODES via mesh-pb-constants.h
#include "TestUtil.h"
#include <unity.h>

#if defined(ARCH_PORTDUINO)
#define NDBR_TEST_ENTRY extern "C"
#else
#define NDBR_TEST_ENTRY
#endif

#include "FSCommon.h"

// This is a disk round-trip suite; without a filesystem there is nothing to pin.
#if defined(FSCom)

#include "mesh/NodeDB.h"
#include <cstdio>
#include <cstring>
#include <pb_encode.h>
#include <vector>

// Friend declared in NodeDB.h (PIO_UNIT_TESTING): exposes the private save path so
// the tests drive exactly the gate under test, without saveToDisk()'s format-retry.
class NodeDBTestShim : public NodeDB
{
  public:
    bool saveDatabase() { return saveNodeDatabaseToDisk(); }
};

namespace
{

NodeDBTestShim *db = nullptr;

/// Simulate a process restart. A real cold boot starts with a zeroed nodeDatabase
/// global; in-process the decode callback would append on top of the previous
/// boot's rows, duplicating every node.
void coldBoot()
{
    if (db) {
        delete db;
        db = nullptr;
        nodeDB = nullptr;
    }
    nodeDatabase.version = 0;
    nodeDatabase.nodes.clear();
    nodeDatabase.positions.clear();
    nodeDatabase.telemetry.clear();
    nodeDatabase.environment.clear();
    nodeDatabase.status.clear();

    db = new NodeDBTestShim();
    nodeDB = db;
}

meshtastic_User makeUser(uint32_t num, uint8_t seed)
{
    meshtastic_User u = meshtastic_User_init_zero;
    snprintf(u.id, sizeof(u.id), "!%08x", num);
    snprintf(u.long_name, sizeof(u.long_name), "Node %02X", seed);
    snprintf(u.short_name, sizeof(u.short_name), "N%02X", seed);
    u.hw_model = meshtastic_HardwareModel_TBEAM;
    u.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    u.public_key.size = 32;
    for (int i = 0; i < 32; i++)
        u.public_key.bytes[i] = (uint8_t)(i ^ seed ^ 0x5A);
    return u;
}

/// Give the node a user so it survives the next boot's cleanupMeshDB() purge -
/// userless, non-ignored rows are dropped on load, which is itself part of the cycle.
meshtastic_NodeInfoLite *addUserNode(uint32_t num, uint8_t seed, uint8_t channelIndex = 0)
{
    meshtastic_User u = makeUser(num, seed);
    nodeDB->updateUser(num, u, channelIndex);
    meshtastic_NodeInfoLite *info = nodeDB->getMeshNode(num);
    TEST_ASSERT_NOT_NULL_MESSAGE(info, "updateUser must admit the node");
    return info;
}

/// A packet as the real over-the-air RX path shapes it: decoded, TRANSPORT_LORA,
/// modern-sender bitfield, rx_time and rx_rssi present.
meshtastic_MeshPacket makeRxPacket(uint32_t from)
{
    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = from;
    mp.to = nodeDB->getNodeNum();
    mp.id = 0x1000u + (from & 0xFFFu);
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    mp.decoded.has_bitfield = true; // modern sender: hop_start is trustworthy
    mp.has_rx_time = true;
    mp.rx_time = 1700000000;
    mp.hop_start = 3;
    mp.hop_limit = 3;
    mp.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    mp.has_rx_rssi = true;
    mp.rx_rssi = -80;
    return mp;
}

void heardOverLoRa(uint32_t from, float snr)
{
    meshtastic_MeshPacket mp = makeRxPacket(from);
    mp.rx_snr = snr;
    nodeDB->updateFrom(mp);
}

meshtastic_StatusMessage makeStatus(const char *text)
{
    meshtastic_StatusMessage st = meshtastic_StatusMessage_init_zero;
    snprintf(st.status, sizeof(st.status), "%s", text);
    return st;
}

bool readFileBytes(const char *path, std::vector<uint8_t> &out)
{
    auto f = FSCom.open(path, FILE_O_READ);
    if (!f)
        return false;
    out.resize(f.size());
    if (!out.empty() && f.read(out.data(), out.size()) != out.size()) {
        f.close();
        return false;
    }
    f.close();
    return true;
}

void decodeNodesFile(meshtastic_NodeDatabase &out)
{
    // _init_zero brace-inits the embedded std::vector via its (size_type) ctor,
    // so callers pass a default-constructed struct; decode targets are disarmed in
    // steady state, so satellite entries land in the struct's own vectors - this
    // reads the on-disk projection directly.
    TEST_ASSERT_EQUAL_MESSAGE(LoadFileResult::LOAD_SUCCESS,
                              db->loadProto(nodeDatabaseFileName, db->getMaxNodesAllocatedSize(), sizeof(meshtastic_NodeDatabase),
                                            &meshtastic_NodeDatabase_msg, &out),
                              "nodes.proto must decode");
}

void assertTempVectorsEmpty(const char *when)
{
    TEST_ASSERT_TRUE_MESSAGE(nodeDatabase.positions.empty(), when);
    TEST_ASSERT_TRUE_MESSAGE(nodeDatabase.telemetry.empty(), when);
    TEST_ASSERT_TRUE_MESSAGE(nodeDatabase.environment.empty(), when);
    TEST_ASSERT_TRUE_MESSAGE(nodeDatabase.status.empty(), when);
}

void clearAllSatellites()
{
    auto wipe = [](const std::vector<NodeNum> &nums) {
        for (NodeNum n : nums)
            nodeDB->eraseNodeSatellites(n);
    };
    wipe(nodeDB->snapshotPositionNodeNums(0));
    wipe(nodeDB->snapshotTelemetryNodeNums(0));
    wipe(nodeDB->snapshotEnvironmentNodeNums(0));
    wipe(nodeDB->snapshotStatusNodeNums(0));
}

} // namespace

void setUp(void) {}
void tearDown(void) {}

// --- Environment preconditions ---

// Every persistence leg depends on boot keygen having produced an owner key
// (keyless devices deliberately skip the nodes.proto write - tested below).
static void test_identityReady_saveUnlocked(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(32, owner.public_key.size, "boot keygen did not run - this suite needs an owner key");
    TEST_ASSERT_NOT_NULL(db->getMeshNode(db->getNodeNum()));
}

// --- updateFrom SNR admission gates (in-RAM policy feeding the persisted bit) ---

static void test_updateFrom_snrTransportGates(void)
{
    const uint32_t A = 0x52000001, B = 0x52000002, C = 0x52000003;

    // Genuine RF reception of a 0 dB packet: stored, and HAS_SNR says so.
    heardOverLoRa(A, 0.0f);
    const meshtastic_NodeInfoLite *na = db->getMeshNode(A);
    TEST_ASSERT_NOT_NULL(na);
    TEST_ASSERT_TRUE_MESSAGE(nodeInfoLiteHasSnr(na), "a measured 0 dB must be recorded as known");
    TEST_ASSERT_EQUAL_FLOAT(0.0f, na->snr);

    // Broker-delivered MQTT packet: rx_snr is not our measurement, never recorded.
    meshtastic_MeshPacket mp = makeRxPacket(B);
    mp.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_MQTT;
    mp.via_mqtt = true;
    mp.rx_snr = 7.5f;
    nodeDB->updateFrom(mp);
    const meshtastic_NodeInfoLite *nb = db->getMeshNode(B);
    TEST_ASSERT_NOT_NULL(nb);
    TEST_ASSERT_FALSE_MESSAGE(nodeInfoLiteHasSnr(nb), "MQTT-transport SNR must not be recorded");
    TEST_ASSERT_EQUAL_FLOAT(0.0f, nb->snr);
    TEST_ASSERT_TRUE(nodeInfoLiteViaMqtt(nb));

    // TRANSPORT_LORA without has_rx_rssi (the PhoneAPI-replay shape): not recorded.
    mp = makeRxPacket(C);
    mp.has_rx_rssi = false;
    mp.rx_rssi = 0;
    mp.rx_snr = 6.0f;
    nodeDB->updateFrom(mp);
    const meshtastic_NodeInfoLite *nc = db->getMeshNode(C);
    TEST_ASSERT_NOT_NULL(nc);
    TEST_ASSERT_FALSE_MESSAGE(nodeInfoLiteHasSnr(nc), "replay-shaped packets must not mint a measurement");
    TEST_ASSERT_EQUAL_FLOAT(0.0f, nc->snr);

    // An MQTT-origin packet a gateway rebroadcast onto LoRa: we measured that one.
    mp = makeRxPacket(B);
    mp.via_mqtt = true;
    mp.rx_snr = -3.5f;
    nodeDB->updateFrom(mp);
    nb = db->getMeshNode(B);
    TEST_ASSERT_TRUE(nodeInfoLiteHasSnr(nb));
    TEST_ASSERT_EQUAL_FLOAT(-3.5f, nb->snr);
}

// --- snr_q4 quantization + HAS_SNR sentinel through a real save/boot cycle ---

static void test_snrQuantization_roundTripsThroughDisk(void)
{
    const uint32_t N1 = 0x53000001; // |SNR| < 0.25 dB: rounds to -1, not truncated to the sentinel
    const uint32_t N2 = 0x53000002; // measured 0.0 dB: the #11271 sentinel collision
    const uint32_t N3 = 0x53000003; // rounds TO 0 yet stays a known measurement
    const uint32_t N4 = 0x53000004; // legacy record: snr set, HAS_SNR clear (compat branch)
    const uint32_t N5 = 0x53000005; // never measured
    const uint32_t N6 = 0x53000006; // plain quantization: 7.9 -> 32/4 = 8.0

    addUserNode(N1, 0x01);
    heardOverLoRa(N1, -0.2f);
    addUserNode(N2, 0x02);
    heardOverLoRa(N2, 0.0f);
    addUserNode(N3, 0x03);
    heardOverLoRa(N3, 0.1f);
    meshtastic_NodeInfoLite *legacy = addUserNode(N4, 0x04);
    legacy->snr = 3.0f; // pre-HAS_SNR store shape: value present, bit clear
    addUserNode(N5, 0x05);
    addUserNode(N6, 0x06);
    heardOverLoRa(N6, 7.9f);

    TEST_ASSERT_TRUE(db->saveDatabase());
    coldBoot();

    const meshtastic_NodeInfoLite *n = db->getMeshNode(N1);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_TRUE(nodeInfoLiteHasSnr(n));
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(-0.25f, n->snr, "lroundf(-0.8) = -1 -> -0.25 dB (rounding, not truncation)");

    n = db->getMeshNode(N2);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_TRUE_MESSAGE(nodeInfoLiteHasSnr(n), "a genuine 0 dB reading must come back as known, not unknown");
    TEST_ASSERT_EQUAL_FLOAT(0.0f, n->snr);

    n = db->getMeshNode(N3);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_TRUE_MESSAGE(nodeInfoLiteHasSnr(n), "a measurement that quantizes to 0 is still a measurement");
    TEST_ASSERT_EQUAL_FLOAT(0.0f, n->snr);

    n = db->getMeshNode(N4);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_FALSE(nodeInfoLiteHasSnr(n));
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(3.0f, n->snr, "legacy snr_q4 without the bit must decode via the compat branch");

    n = db->getMeshNode(N5);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_FALSE_MESSAGE(nodeInfoLiteHasSnr(n), "snr_q4 = 0 with the bit clear is unambiguously unknown");
    TEST_ASSERT_EQUAL_FLOAT(0.0f, n->snr);

    n = db->getMeshNode(N6);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_TRUE(nodeInfoLiteHasSnr(n));
    TEST_ASSERT_EQUAL_FLOAT(8.0f, n->snr);
}

// --- Full header + satellite-map projection/rehydration cycle ---

static void test_fullRoundTrip_headerAndSatelliteFidelity(void)
{
    const uint32_t P = 0x54000001; // position
    const uint32_t T = 0x54000002; // device telemetry
    const uint32_t E = 0x54000003; // environment + status
    const uint32_t M = 0x54000004; // bitfield bools + hops

    addUserNode(P, 0x11, /*channelIndex=*/2);
    heardOverLoRa(P, 5.5f);
#if !MESHTASTIC_EXCLUDE_POSITIONDB
    meshtastic_Position pos = meshtastic_Position_init_zero;
    pos.latitude_i = 375000000;
    pos.longitude_i = -1219876543;
    pos.altitude = 123;
    pos.time = 1700000200;
    pos.location_source = meshtastic_Position_LocSource_LOC_INTERNAL;
    pos.precision_bits = 32;
    nodeDB->updatePosition(P, pos);
#endif

    addUserNode(T, 0x12);
#if !MESHTASTIC_EXCLUDE_TELEMETRYDB
    meshtastic_Telemetry tel = meshtastic_Telemetry_init_zero;
    tel.which_variant = meshtastic_Telemetry_device_metrics_tag;
    tel.variant.device_metrics.has_battery_level = true;
    tel.variant.device_metrics.battery_level = 87;
    tel.variant.device_metrics.has_voltage = true;
    tel.variant.device_metrics.voltage = 3.7f;
    tel.variant.device_metrics.has_channel_utilization = true;
    tel.variant.device_metrics.channel_utilization = 12.5f;
    tel.variant.device_metrics.has_air_util_tx = true;
    tel.variant.device_metrics.air_util_tx = 1.5f;
    tel.variant.device_metrics.has_uptime_seconds = true;
    tel.variant.device_metrics.uptime_seconds = 3600;
    nodeDB->updateTelemetry(T, tel);
#endif

    addUserNode(E, 0x13);
#if !MESHTASTIC_EXCLUDE_ENVIRONMENTDB
    meshtastic_Telemetry env = meshtastic_Telemetry_init_zero;
    env.which_variant = meshtastic_Telemetry_environment_metrics_tag;
    env.variant.environment_metrics.has_temperature = true;
    env.variant.environment_metrics.temperature = 21.5f;
    env.variant.environment_metrics.has_relative_humidity = true;
    env.variant.environment_metrics.relative_humidity = 40.5f;
    env.variant.environment_metrics.has_barometric_pressure = true;
    env.variant.environment_metrics.barometric_pressure = 1013.25f;
    nodeDB->updateTelemetry(E, env);
#endif
#if !MESHTASTIC_EXCLUDE_STATUSDB
    nodeDB->setNodeStatus(E, makeStatus("on the tower"));
#endif

    meshtastic_NodeInfoLite *m = addUserNode(M, 0x14);
    meshtastic_MeshPacket mp = makeRxPacket(M);
    mp.via_mqtt = true; // gateway rebroadcast: bit stored, SNR still ours
    mp.hop_start = 5;
    mp.hop_limit = 2; // hops_away = 3
    mp.rx_snr = 2.0f;
    nodeDB->updateFrom(mp);
    m = db->getMeshNode(M);
    nodeInfoLiteSetBit(m, NODEINFO_BITFIELD_IS_MUTED_MASK, true);

    TEST_ASSERT_TRUE(db->saveDatabase());
    assertTempVectorsEmpty("temp vectors must be cleared after the save projection");

    coldBoot();
    assertTempVectorsEmpty("armed decode must route entries into the maps, not the temp vectors");

    // Header fidelity
    const meshtastic_NodeInfoLite *np = db->getMeshNode(P);
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING("Node 11", np->long_name);
    TEST_ASSERT_EQUAL_STRING("N11", np->short_name);
    TEST_ASSERT_EQUAL(meshtastic_HardwareModel_TBEAM, np->hw_model);
    TEST_ASSERT_EQUAL_UINT8(2, np->channel);
    TEST_ASSERT_EQUAL_UINT32(1700000000, np->last_heard);
    TEST_ASSERT_TRUE(nodeInfoLiteHasSnr(np));
    TEST_ASSERT_EQUAL_FLOAT(5.5f, np->snr);
    meshtastic_User expected = makeUser(P, 0x11);
    TEST_ASSERT_EQUAL(32, np->public_key.size);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(expected.public_key.bytes, np->public_key.bytes, 32,
                                     "public key must survive byte-identical");

    const meshtastic_NodeInfoLite *nm = db->getMeshNode(M);
    TEST_ASSERT_NOT_NULL(nm);
    TEST_ASSERT_TRUE(nodeInfoLiteViaMqtt(nm));
    TEST_ASSERT_TRUE(nodeInfoLiteIsMuted(nm));
    TEST_ASSERT_TRUE(nm->has_hops_away);
    TEST_ASSERT_EQUAL_UINT8(3, nm->hops_away);
    TEST_ASSERT_TRUE(nodeInfoLiteHasSnr(nm));
    TEST_ASSERT_EQUAL_FLOAT(2.0f, nm->snr);

    // Satellite rehydration - identical values, and only where they were written.
#if !MESHTASTIC_EXCLUDE_POSITIONDB
    meshtastic_PositionLite gotPos;
    TEST_ASSERT_TRUE(db->copyNodePosition(P, gotPos));
    TEST_ASSERT_EQUAL_INT32(375000000, gotPos.latitude_i);
    TEST_ASSERT_EQUAL_INT32(-1219876543, gotPos.longitude_i);
    TEST_ASSERT_EQUAL_INT32(123, gotPos.altitude);
    TEST_ASSERT_EQUAL_UINT32(1700000200, gotPos.time);
    TEST_ASSERT_EQUAL(meshtastic_Position_LocSource_LOC_INTERNAL, gotPos.location_source);
    TEST_ASSERT_EQUAL_UINT32(32, gotPos.precision_bits);
    TEST_ASSERT_FALSE_MESSAGE(db->hasNodePosition(T), "no position was ever written for T");
#endif

#if !MESHTASTIC_EXCLUDE_TELEMETRYDB
    meshtastic_DeviceMetrics gotDm;
    TEST_ASSERT_TRUE(db->copyNodeTelemetry(T, gotDm));
    TEST_ASSERT_TRUE(gotDm.has_battery_level);
    TEST_ASSERT_EQUAL_UINT32(87, gotDm.battery_level);
    TEST_ASSERT_TRUE(gotDm.has_voltage);
    TEST_ASSERT_EQUAL_FLOAT(3.7f, gotDm.voltage);
    TEST_ASSERT_TRUE(gotDm.has_channel_utilization);
    TEST_ASSERT_EQUAL_FLOAT(12.5f, gotDm.channel_utilization);
    TEST_ASSERT_TRUE(gotDm.has_air_util_tx);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, gotDm.air_util_tx);
    TEST_ASSERT_TRUE(gotDm.has_uptime_seconds);
    TEST_ASSERT_EQUAL_UINT32(3600, gotDm.uptime_seconds);
    TEST_ASSERT_FALSE(db->hasNodeTelemetry(P));
#endif

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTDB
    meshtastic_EnvironmentMetrics gotEnv;
    TEST_ASSERT_TRUE(db->copyNodeEnvironment(E, gotEnv));
    TEST_ASSERT_TRUE(gotEnv.has_temperature);
    TEST_ASSERT_EQUAL_FLOAT(21.5f, gotEnv.temperature);
    TEST_ASSERT_TRUE(gotEnv.has_relative_humidity);
    TEST_ASSERT_EQUAL_FLOAT(40.5f, gotEnv.relative_humidity);
    TEST_ASSERT_TRUE(gotEnv.has_barometric_pressure);
    TEST_ASSERT_EQUAL_FLOAT(1013.25f, gotEnv.barometric_pressure);
#endif

#if !MESHTASTIC_EXCLUDE_STATUSDB
    meshtastic_StatusMessage gotSt;
    TEST_ASSERT_TRUE(db->copyNodeStatus(E, gotSt));
    TEST_ASSERT_EQUAL_STRING("on the tower", gotSt.status);
    TEST_ASSERT_FALSE(db->hasNodeStatus(P));
#endif
}

// --- Keyless-save skip (part of the PKI-DM key-amnesia diagnosis) ---

static void test_keylessDevice_skipsNodesProtoWrite(void)
{
#if MESHTASTIC_EXCLUDE_PKI_KEYGEN || MESHTASTIC_EXCLUDE_PKI
    TEST_IGNORE_MESSAGE("keyless-save gate compiled out on this build");
#else
    std::vector<uint8_t> before;
    TEST_ASSERT_TRUE_MESSAGE(readFileBytes(nodeDatabaseFileName, before), "nodes.proto must exist before the gate check");

    const meshtastic_User_public_key_t savedKey = owner.public_key;
    const bool savedLicensed = owner.is_licensed;
    owner.public_key.size = 0;
    owner.is_licensed = false;

    // Returning success on the skip matters: a false here would propagate into
    // saveToDisk()'s fsFormat() whole-FS wipe.
    TEST_ASSERT_TRUE_MESSAGE(db->saveDatabase(), "keyless save must report success");

    std::vector<uint8_t> after;
    TEST_ASSERT_TRUE(readFileBytes(nodeDatabaseFileName, after));
    TEST_ASSERT_TRUE_MESSAGE(before == after, "keyless save must leave nodes.proto byte-identical");

    owner.public_key = savedKey;
    owner.is_licensed = savedLicensed;

    // Control: with the key restored, the same call writes.
    addUserNode(0x55000001, 0x55);
    TEST_ASSERT_TRUE(db->saveDatabase());
    TEST_ASSERT_TRUE(readFileBytes(nodeDatabaseFileName, after));
    TEST_ASSERT_FALSE_MESSAGE(before == after, "keyed save must rewrite nodes.proto");
#endif
}

// --- Live satellite-cap eviction policy ---

#if !MESHTASTIC_EXCLUDE_STATUSDB
static void test_satelliteCap_evictionPolicy(void)
{
    if ((size_t)MAX_NUM_NODES < (size_t)MAX_SATELLITE_NODES + 8)
        TEST_IGNORE_MESSAGE("hot cap too small to own a full satellite map on this build");

    clearAllSatellites();
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)nodeDB->snapshotStatusNodeNums(0).size());

    const NodeNum self = nodeDB->getNodeNum();
    meshtastic_NodeInfoLite *selfRow = nodeDB->getOrCreateMeshNode(self);
    TEST_ASSERT_NOT_NULL(selfRow);
    selfRow->last_heard = 0; // stalest possible: only the identity exemption can protect it
    nodeDB->setNodeStatus(self, makeStatus("self"));

    // Fill to exactly the cap with hot-owned entries; owner i heard at 1000+i.
    const size_t owners = (size_t)MAX_SATELLITE_NODES - 1;
    const NodeNum ownerBase = 0x60000000u;
    for (size_t i = 0; i < owners; i++) {
        meshtastic_NodeInfoLite *info = nodeDB->getOrCreateMeshNode(ownerBase + i);
        TEST_ASSERT_NOT_NULL(info);
        info->last_heard = 1000 + (uint32_t)i;
        nodeDB->setNodeStatus(ownerBase + i, makeStatus("owned"));
    }
    TEST_ASSERT_EQUAL_UINT((unsigned)MAX_SATELLITE_NODES, (unsigned)nodeDB->snapshotStatusNodeNums(0).size());

    // (a) At cap, a new entry evicts the stalest-by-owner victim - never self,
    // even though self ranks stalest of all.
    const NodeNum orphan1 = 0x60FFFF01u;
    nodeDB->setNodeStatus(orphan1, makeStatus("new"));
    TEST_ASSERT_TRUE_MESSAGE(db->hasNodeStatus(self), "self must never be evicted");
    TEST_ASSERT_FALSE_MESSAGE(db->hasNodeStatus(ownerBase + 0), "stalest owner must be the victim");
    TEST_ASSERT_TRUE(db->hasNodeStatus(ownerBase + 1));
    TEST_ASSERT_TRUE(db->hasNodeStatus(orphan1));
    TEST_ASSERT_EQUAL_UINT((unsigned)MAX_SATELLITE_NODES, (unsigned)nodeDB->snapshotStatusNodeNums(0).size());

    // (b) Orphans (owner absent from the hot store) are evicted before any owner,
    // however stale the owner: orphan1 (recency 0) loses to owner1 (1001).
    const NodeNum orphan2 = 0x60FFFF02u;
    nodeDB->setNodeStatus(orphan2, makeStatus("new2"));
    TEST_ASSERT_FALSE_MESSAGE(db->hasNodeStatus(orphan1), "orphan must be evicted before any owned entry");
    TEST_ASSERT_TRUE(db->hasNodeStatus(ownerBase + 1));
    TEST_ASSERT_TRUE(db->hasNodeStatus(orphan2));
    TEST_ASSERT_EQUAL_UINT((unsigned)MAX_SATELLITE_NODES, (unsigned)nodeDB->snapshotStatusNodeNums(0).size());

    // (c) Updating an existing key at cap must not evict anything.
    nodeDB->setNodeStatus(ownerBase + 1, makeStatus("updated"));
    TEST_ASSERT_EQUAL_UINT((unsigned)MAX_SATELLITE_NODES, (unsigned)nodeDB->snapshotStatusNodeNums(0).size());
    TEST_ASSERT_TRUE_MESSAGE(db->hasNodeStatus(orphan2), "update-in-place must not trigger eviction");
    meshtastic_StatusMessage got;
    TEST_ASSERT_TRUE(db->copyNodeStatus(ownerBase + 1, got));
    TEST_ASSERT_EQUAL_STRING("updated", got.status);
}
#endif // !MESHTASTIC_EXCLUDE_STATUSDB

// --- Boot-time trim of an over-cap nodes.proto (capacity downgrade / foreign file) ---

#if !MESHTASTIC_EXCLUDE_POSITIONDB
static void test_bootTrim_overCapSatellitesHealedOnDisk(void)
{
    const size_t overBy = 10;
    const NodeNum base = 0x70000000u;

    // Craft a v25 nodes.proto whose position store exceeds this build's cap, as a
    // larger-cap build (or a peer backup) would leave behind.
    meshtastic_NodeDatabase crafted{};
    crafted.version = DEVICESTATE_CUR_VER;
    for (size_t i = 0; i < (size_t)MAX_SATELLITE_NODES + overBy; i++) {
        meshtastic_NodePositionEntry e = meshtastic_NodePositionEntry_init_zero;
        e.num = base + (uint32_t)i;
        e.has_position = true;
        e.position.latitude_i = (int32_t)(1000 + i);
        e.position.time = 1000 + (uint32_t)i;
        crafted.positions.push_back(e);
    }
    size_t craftedSize = 0;
    TEST_ASSERT_TRUE(pb_get_encoded_size(&craftedSize, meshtastic_NodeDatabase_fields, &crafted));
    TEST_ASSERT_TRUE(db->saveProto(nodeDatabaseFileName, craftedSize, &meshtastic_NodeDatabase_msg, &crafted, false));

    coldBoot();

    // Trimmed in RAM to exactly the cap; all entries were orphans, so the
    // lowest-recency victims (here: the lowest-numbered) went first.
    TEST_ASSERT_EQUAL_UINT((unsigned)MAX_SATELLITE_NODES, (unsigned)nodeDB->snapshotPositionNodeNums(0).size());
    TEST_ASSERT_TRUE(db->hasNodePosition(base + (uint32_t)MAX_SATELLITE_NODES + (uint32_t)overBy - 1));
    TEST_ASSERT_FALSE(db->hasNodePosition(base));

    // And healed on disk: nodeDBSelfCare rewrote the store once during the boot.
    meshtastic_NodeDatabase reloaded{};
    decodeNodesFile(reloaded);
    size_t persisted = 0;
    for (const auto &e : reloaded.positions)
        if (e.has_position)
            persisted++;
    TEST_ASSERT_EQUAL_UINT_MESSAGE((unsigned)MAX_SATELLITE_NODES, (unsigned)persisted,
                                   "boot must rewrite the over-cap store trimmed");
}
#endif // !MESHTASTIC_EXCLUDE_POSITIONDB

// --- resetNodes(keepFavorites): no ghost rows above numMeshNodes ---

static void test_resetNodesKeepFavorites_compactsWithoutGhostRows(void)
{
    const uint32_t F1 = 0x71000001, F2 = 0x71000002, F3 = 0x71000003, F4 = 0x71000004;
    addUserNode(F1, 0x21);
    addUserNode(F2, 0x22);
    addUserNode(F3, 0x23);
    addUserNode(F4, 0x24);
    TEST_ASSERT_TRUE(nodeDB->set_favorite(true, F2));
    TEST_ASSERT_TRUE(nodeDB->set_favorite(true, F4));
#if !MESHTASTIC_EXCLUDE_POSITIONDB
    meshtastic_Position pos = meshtastic_Position_init_zero;
    pos.latitude_i = 111;
    pos.longitude_i = 222;
    nodeDB->updatePosition(F1, pos);
    nodeDB->updatePosition(F2, pos);
#endif

    nodeDB->resetNodes(/*keepFavorites=*/true);

    // RAM: self + the two favorites, compacted into contiguous low slots.
    TEST_ASSERT_EQUAL_INT(3, (int)nodeDB->getNumMeshNodes());
    TEST_ASSERT_NULL(db->getMeshNode(F1));
    TEST_ASSERT_NULL(db->getMeshNode(F3));
    const meshtastic_NodeInfoLite *f2 = db->getMeshNode(F2);
    const meshtastic_NodeInfoLite *f4 = db->getMeshNode(F4);
    TEST_ASSERT_NOT_NULL(f2);
    TEST_ASSERT_NOT_NULL(f4);
    TEST_ASSERT_TRUE(nodeInfoLiteIsFavorite(f2));
    TEST_ASSERT_TRUE(nodeInfoLiteIsFavorite(f4));
#if !MESHTASTIC_EXCLUDE_POSITIONDB
    TEST_ASSERT_FALSE_MESSAGE(db->hasNodePosition(F1), "non-favorite satellites must be dropped");
    TEST_ASSERT_TRUE_MESSAGE(db->hasNodePosition(F2), "favorite satellites must survive");
#endif

    // Disk: resetNodes saved; the serialized store must carry the favorites in
    // the low slots and NOTHING above numMeshNodes - a zeroed-in-place favorite
    // would be invisible to every scan yet still serialized (the ghost bug).
    meshtastic_NodeDatabase reloaded{};
    decodeNodesFile(reloaded);
    TEST_ASSERT_TRUE(reloaded.nodes.size() >= 3);
    size_t liveRows = 0;
    bool sawF2 = false, sawF4 = false, sawSelf = false;
    for (size_t i = 0; i < reloaded.nodes.size(); i++) {
        const meshtastic_NodeInfoLite &row = reloaded.nodes[i];
        if (row.num == 0)
            continue;
        liveRows++;
        TEST_ASSERT_TRUE_MESSAGE(i < 3, "live row serialized above numMeshNodes: a ghost entry");
        if (row.num == F2)
            sawF2 = true;
        if (row.num == F4)
            sawF4 = true;
        if (row.num == nodeDB->getNodeNum())
            sawSelf = true;
    }
    TEST_ASSERT_EQUAL_UINT(3, (unsigned)liveRows);
    TEST_ASSERT_TRUE(sawSelf);
    TEST_ASSERT_TRUE(sawF2);
    TEST_ASSERT_TRUE(sawF4);
}

NDBR_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
#if defined(ARCH_PORTDUINO)
    // The stalest-owner eviction case needs hot capacity above the satellite cap
    // (the real large-flash topology). Set before the first NodeDB so every boot
    // in this suite sees one consistent cap.
    portduino_config.MaxNodes = (int)MAX_SATELLITE_NODES + 50;
#endif
    // First boot on the empty sandbox: installs defaults, runs keygen, and
    // persists the base config files every later cold boot reloads.
    coldBoot();

#if !(MESHTASTIC_EXCLUDE_PKI_KEYGEN || MESHTASTIC_EXCLUDE_PKI)
    // Boot keygen is region-gated on real radios (simradio bypasses the gate);
    // if this environment blocked it, set a region and mint the identity now so
    // the persistence legs run instead of cascading off a locked save.
    if (owner.public_key.size != 32) {
        config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
        nodeDB->generateCryptoKeyPair(nullptr);
    }
#endif

    UNITY_BEGIN();

    printf("\n=== Preconditions ===\n");
    RUN_TEST(test_identityReady_saveUnlocked);

    printf("\n=== updateFrom SNR gates ===\n");
    RUN_TEST(test_updateFrom_snrTransportGates);

    printf("\n=== snr_q4 + HAS_SNR round trip ===\n");
    RUN_TEST(test_snrQuantization_roundTripsThroughDisk);

    printf("\n=== Satellite projection/rehydration ===\n");
    RUN_TEST(test_fullRoundTrip_headerAndSatelliteFidelity);

    printf("\n=== Keyless-save gate ===\n");
    RUN_TEST(test_keylessDevice_skipsNodesProtoWrite);

    printf("\n=== Satellite caps ===\n");
#if !MESHTASTIC_EXCLUDE_STATUSDB
    RUN_TEST(test_satelliteCap_evictionPolicy);
#endif
#if !MESHTASTIC_EXCLUDE_POSITIONDB
    RUN_TEST(test_bootTrim_overCapSatellitesHealedOnDisk);
#endif

    printf("\n=== resetNodes ghost rows ===\n");
    RUN_TEST(test_resetNodesKeepFavorites_compactsWithoutGhostRows);

    exit(UNITY_END());
}
NDBR_TEST_ENTRY void loop() {}

#else // !FSCom - no filesystem, nothing to round-trip

void setUp(void) {}
void tearDown(void) {}

NDBR_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}
NDBR_TEST_ENTRY void loop() {}

#endif
