// The one-shot v24 -> v25 NodeDatabase migration every 2.7 -> 2.8 upgrader runs: each test
// hand-encodes a legacy /prefs/nodes.proto, cold-boots a real NodeDB, and asserts the migrated
// state (including sanitizeUtf8 of legacy names, which the later encode depends on).
#include "MeshTypes.h" // BEFORE TestUtil.h - provides MAX_NUM_NODES via mesh-pb-constants.h
#include "TestUtil.h"
#include <unity.h>

#if defined(ARCH_PORTDUINO)
#define NDBM_TEST_ENTRY extern "C"
#else
#define NDBM_TEST_ENTRY
#endif

#include "FSCommon.h"

// The migration is a file-load path; without a filesystem there is nothing to drive.
#if defined(FSCom)

#include "mesh/NodeDB.h"
#include "mesh/generated/meshtastic/deviceonly_legacy.pb.h"
#include "meshUtils.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <pb_decode.h>
#include <pb_encode.h>
#include <string>
#include <vector>

// Exposes the private save path via the friend declaration in NodeDB.h, so the
// hostile-name test can prove the migrated store re-encodes cleanly.
class NodeDBTestShim : public NodeDB
{
  public:
    bool saveDatabase() { return saveNodeDatabaseToDisk(); }
};

namespace
{

NodeDBTestShim *db = nullptr;

void fillKey(meshtastic_UserLite_public_key_t &key, uint8_t seed)
{
    key.size = 32;
    for (int i = 0; i < 32; i++)
        key.bytes[i] = (uint8_t)(i ^ seed);
    key.bytes[0] = seed; // distinctive, never all-zero
}

meshtastic_NodeInfoLite_Legacy makeLegacyNode(uint32_t num, uint32_t lastHeard)
{
    meshtastic_NodeInfoLite_Legacy n = meshtastic_NodeInfoLite_Legacy_init_zero;
    n.num = num;
    n.last_heard = lastHeard;
    return n;
}

void giveLegacyUser(meshtastic_NodeInfoLite_Legacy &n, const char *longName, const char *shortName)
{
    n.has_user = true;
    strncpy(n.user.long_name, longName, sizeof(n.user.long_name));
    n.user.long_name[sizeof(n.user.long_name) - 1] = '\0';
    strncpy(n.user.short_name, shortName, sizeof(n.user.short_name));
    n.user.short_name[sizeof(n.user.short_name) - 1] = '\0';
}

/// Encode a legacy-shape NodeDatabase - exactly what a 2.7 device leaves
/// behind for the 2.8 boot to find.
std::vector<uint8_t> encodeLegacyNodes(uint32_t version, const std::vector<meshtastic_NodeInfoLite_Legacy> &nodes)
{
    // _init_zero brace-inits the embedded std::vector via its explicit
    // (size_type, allocator) ctor, so default-construct instead (see
    // NodeDBLegacyMigration.cpp).
    meshtastic_NodeDatabase_Legacy legacyDb{};
    legacyDb.version = version;
    legacyDb.nodes = nodes;

    size_t encodedSize = 0;
    TEST_ASSERT_TRUE_MESSAGE(pb_get_encoded_size(&encodedSize, meshtastic_NodeDatabase_Legacy_fields, &legacyDb),
                             "sizing the legacy fixture must succeed");
    std::vector<uint8_t> buf(encodedSize);
    pb_ostream_t stream = pb_ostream_from_buffer(buf.data(), buf.size());
    TEST_ASSERT_TRUE_MESSAGE(pb_encode(&stream, meshtastic_NodeDatabase_Legacy_fields, &legacyDb),
                             "encoding the legacy fixture must succeed");
    buf.resize(stream.bytes_written);
    return buf;
}

void writeNodesBytes(const uint8_t *bytes, size_t len)
{
    FSCom.mkdir("/prefs");
    FSCom.remove(nodeDatabaseFileName);
    auto f = FSCom.open(nodeDatabaseFileName, FILE_O_WRITE);
    TEST_ASSERT_TRUE((bool)f);
    const size_t wrote = f.write(bytes, len);
    f.close();
    TEST_ASSERT_EQUAL_MESSAGE(len, wrote, "short write laying down the nodes.proto fixture");
}

void writeLegacyNodesFile(uint32_t version, const std::vector<meshtastic_NodeInfoLite_Legacy> &nodes)
{
    const std::vector<uint8_t> buf = encodeLegacyNodes(version, nodes);
    writeNodesBytes(buf.data(), buf.size());
}

/// Overwrite a unique same-length placeholder inside an encoded fixture with
/// raw bytes. PB_VALIDATE_UTF8 makes pb_encode refuse invalid UTF-8, so a
/// hostile v24 name (written by pre-validation firmware) can only be produced
/// by patching the encoded bytes - the protobuf framing stays intact because
/// the length does not change.
void patchBytes(std::vector<uint8_t> &buf, const char *placeholder, const char *raw, size_t n)
{
    TEST_ASSERT_EQUAL(strlen(placeholder), n);
    auto it = std::search(buf.begin(), buf.end(), reinterpret_cast<const uint8_t *>(placeholder),
                          reinterpret_cast<const uint8_t *>(placeholder) + n);
    TEST_ASSERT_TRUE_MESSAGE(it != buf.end(), "placeholder not found in encoded fixture");
    memcpy(&*it, raw, n);
}

/// Simulate a process restart. A real cold boot starts with a zeroed
/// nodeDatabase global; in-process it still holds the previous boot's version
/// stamp and nodes, which would short-circuit the version-gate ladder.
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

/// The migrated-store re-save (migrationSavePending) is skipped for keyless
/// devices, so every persistence assertion depends on boot keygen having run.
void assertBootKeygenRan()
{
    TEST_ASSERT_EQUAL_MESSAGE(32, owner.public_key.size,
                              "boot keygen did not run - persistence legs of this suite need an owner key");
}

/// True UTF-8 cleanliness check via the production validator: a second
/// sanitize pass over already-sanitized bytes must find nothing to replace.
void assertValidUtf8(const char *s, size_t width)
{
    char copy[64];
    TEST_ASSERT_TRUE(width < sizeof(copy));
    memcpy(copy, s, width);
    TEST_ASSERT_FALSE_MESSAGE(sanitizeUtf8(copy, width), "migrated name still contains invalid UTF-8");
}

void writeModuleBytes(const uint8_t *bytes, size_t len)
{
    FSCom.mkdir("/prefs");
    FSCom.remove(moduleConfigFileName);
    auto f = FSCom.open(moduleConfigFileName, FILE_O_WRITE);
    TEST_ASSERT_TRUE((bool)f);
    const size_t wrote = f.write(bytes, len);
    f.close();
    TEST_ASSERT_EQUAL_MESSAGE(len, wrote, "short write laying down the module.proto fixture");
}

/// A 2.8.0 module.proto from before the broadcast_message cut: base's fields, with a mesh_beacon whose
/// message is longer than the current struct holds. padTo > 0 appends an unknown field to that total.
std::vector<uint8_t> encodePreCutModuleConfig(const meshtastic_LocalModuleConfig &base, const std::string &message,
                                              size_t padTo = 0)
{
    meshtastic_LocalModuleConfig outer = base;
    outer.has_mesh_beacon = false;
    std::vector<uint8_t> buf(2048);
    pb_ostream_t os = pb_ostream_from_buffer(buf.data(), buf.size());
    TEST_ASSERT_TRUE(pb_encode(&os, meshtastic_LocalModuleConfig_fields, &outer));

    meshtastic_ModuleConfig_MeshBeaconConfig beacon = base.mesh_beacon;
    beacon.broadcast_message[0] = '\0'; // the long message is written by hand below
    uint8_t body[512];
    pb_ostream_t bs = pb_ostream_from_buffer(body, sizeof(body));
    TEST_ASSERT_TRUE(pb_encode(&bs, meshtastic_ModuleConfig_MeshBeaconConfig_fields, &beacon));
    TEST_ASSERT_TRUE(pb_encode_tag(&bs, PB_WT_STRING, meshtastic_ModuleConfig_MeshBeaconConfig_broadcast_message_tag));
    TEST_ASSERT_TRUE(pb_encode_string(&bs, reinterpret_cast<const pb_byte_t *>(message.data()), message.size()));
    TEST_ASSERT_TRUE(pb_encode_tag(&os, PB_WT_STRING, meshtastic_LocalModuleConfig_mesh_beacon_tag));
    TEST_ASSERT_TRUE(pb_encode_string(&os, body, bs.bytes_written));

    if (padTo) {
        const size_t overhead = 4; // 2-byte key for tag 1000, 2-byte length
        TEST_ASSERT_TRUE_MESSAGE(padTo >= os.bytes_written + overhead + 128, "padding must need a 2-byte length");
        const std::vector<uint8_t> pad(padTo - os.bytes_written - overhead, 0x55);
        TEST_ASSERT_TRUE(pb_encode_tag(&os, PB_WT_STRING, 1000));
        TEST_ASSERT_TRUE(pb_encode_string(&os, pad.data(), pad.size()));
    }
    buf.resize(os.bytes_written);
    return buf;
}

/// The settings every module.proto case writes, so a wiped config is told apart from a migrated one.
meshtastic_LocalModuleConfig distinctiveModuleConfig()
{
    meshtastic_LocalModuleConfig c = moduleConfig; // the first boot's defaults, with a current version
    c.has_mqtt = true;
    c.mqtt.enabled = true;
    strncpy(c.mqtt.address, "mqtt.example", sizeof(c.mqtt.address) - 1);
    c.has_telemetry = true;
    c.telemetry.device_update_interval = 7200; // above the floor boot coerces default-channel telemetry to
    c.has_mesh_beacon = true;
    c.mesh_beacon.broadcast_interval_secs = 7200;
    c.mesh_beacon.has_broadcast_offer_channel = true;
    strncpy(c.mesh_beacon.broadcast_offer_channel.name, "Open", sizeof(c.mesh_beacon.broadcast_offer_channel.name) - 1);
    c.mesh_beacon.broadcast_offer_channel.psk.size = 1;
    c.mesh_beacon.broadcast_offer_channel.psk.bytes[0] = 1;
    return c;
}

void assertDistinctiveModulesSurvived(const meshtastic_LocalModuleConfig &c)
{
    TEST_ASSERT_TRUE_MESSAGE(c.mqtt.enabled, "MQTT must survive the migration, not reset to defaults");
    TEST_ASSERT_EQUAL_STRING("mqtt.example", c.mqtt.address);
    TEST_ASSERT_EQUAL_UINT32(7200, c.telemetry.device_update_interval);
    TEST_ASSERT_TRUE(c.has_mesh_beacon);
    TEST_ASSERT_EQUAL_UINT32(7200, c.mesh_beacon.broadcast_interval_secs);
    TEST_ASSERT_EQUAL_STRING("Open", c.mesh_beacon.broadcast_offer_channel.name);
}

} // namespace

void setUp(void) {}
void tearDown(void) {}

// --- Version-gate ladder (NodeDB.cpp loadFromDisk) ---

// v24 with no nodes is still a migration: the version stamp must advance and
// the boot must complete with just ourself in the store.
static void test_emptyV24File_migratesToEmptyV25(void)
{
    writeLegacyNodesFile(24, {});
    coldBoot();

    TEST_ASSERT_EQUAL_UINT32(DEVICESTATE_CUR_VER, nodeDatabase.version);
    TEST_ASSERT_EQUAL_INT(1, (int)db->getNumMeshNodes()); // self only, added by nodeDBSelfCare
}

// version < DEVICESTATE_MIN_VER: discarded, never migrated.
static void test_versionBelowMin_discardsToDefaults(void)
{
    auto old = makeLegacyNode(0xF6000001, 1000);
    giveLegacyUser(old, "Ancient", "OLD");
    writeLegacyNodesFile(DEVICESTATE_MIN_VER - 1, {old});
    coldBoot();

    TEST_ASSERT_NULL(db->getMeshNode(0xF6000001));
    TEST_ASSERT_EQUAL_UINT32(DEVICESTATE_CUR_VER, nodeDatabase.version);
    TEST_ASSERT_EQUAL_INT(1, (int)db->getNumMeshNodes());
}

// Garbage bytes: the v25 decode fails, the version stays below MIN, and the
// boot lands on installDefaultNodeDatabase instead of crashing or migrating.
static void test_garbageNodesProto_installsDefaults(void)
{
    static const uint8_t garbage[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x13, 0x37, 0xC0, 0xFF, 0xEE};
    writeNodesBytes(garbage, sizeof(garbage));
    coldBoot();

    TEST_ASSERT_EQUAL_UINT32(DEVICESTATE_CUR_VER, nodeDatabase.version);
    TEST_ASSERT_EQUAL_INT(1, (int)db->getNumMeshNodes());
}

// --- Field-by-field migration fidelity ---

static void test_v24RoundTrip_migratesFieldsBitfieldAndSatellites(void)
{
    std::vector<meshtastic_NodeInfoLite_Legacy> nodes;

    // Node A: every scalar populated, plus position + device_metrics.
    auto a = makeLegacyNode(0xA1000001, 111111);
    giveLegacyUser(a, "Alice Node", "AL");
    a.user.hw_model = meshtastic_HardwareModel_TBEAM;
    a.user.role = meshtastic_Config_DeviceConfig_Role_TRACKER;
    fillKey(a.user.public_key, 0x42);
    a.snr = 7.25f;
    a.channel = 2;
    a.has_hops_away = true;
    a.hops_away = 3;
    a.next_hop = 0xAB;
    a.has_position = true;
    a.position.latitude_i = 375000000;
    a.position.longitude_i = -1219876543;
    a.position.altitude = 123;
    a.position.time = 1700000000;
    a.position.location_source = meshtastic_Position_LocSource_LOC_INTERNAL;
    a.position.precision_bits = 32;
    a.has_device_metrics = true;
    a.device_metrics.has_battery_level = true;
    a.device_metrics.battery_level = 87;
    a.device_metrics.has_voltage = true;
    a.device_metrics.voltage = 3.7f;
    nodes.push_back(a);

    // Node B: the legacy compatibility bools that must pack into the bitfield.
    auto b = makeLegacyNode(0xA1000002, 222222);
    giveLegacyUser(b, "Bob", "BB");
    b.via_mqtt = true;
    b.is_favorite = true;
    nodes.push_back(b);

    // Node C: blocked + licensed.
    auto c = makeLegacyNode(0xA1000003, 333333);
    giveLegacyUser(c, "Carol", "CC");
    c.is_ignored = true;
    c.user.is_licensed = true;
    nodes.push_back(c);

    // Node D: tri-state unmessagable present-and-set.
    auto d = makeLegacyNode(0xA1000004, 444444);
    giveLegacyUser(d, "Dave", "DD");
    d.user.has_is_unmessagable = true;
    d.user.is_unmessagable = true;
    nodes.push_back(d);

    // Node E: control - no key, no bools, no unmessagable tri-state.
    auto e = makeLegacyNode(0xA1000005, 555555);
    giveLegacyUser(e, "Erin", "EE");
    nodes.push_back(e);

    writeLegacyNodesFile(24, nodes);
    coldBoot();

    TEST_ASSERT_EQUAL_UINT32(DEVICESTATE_CUR_VER, nodeDatabase.version);
    TEST_ASSERT_EQUAL_INT(6, (int)db->getNumMeshNodes()); // 5 migrated + self

    const meshtastic_NodeInfoLite *na = db->getMeshNode(0xA1000001);
    TEST_ASSERT_NOT_NULL(na);
    TEST_ASSERT_EQUAL_STRING("Alice Node", na->long_name);
    TEST_ASSERT_EQUAL_STRING("AL", na->short_name);
    TEST_ASSERT_EQUAL(meshtastic_HardwareModel_TBEAM, na->hw_model);
    TEST_ASSERT_EQUAL(meshtastic_Config_DeviceConfig_Role_TRACKER, na->role);
    TEST_ASSERT_EQUAL_FLOAT(7.25f, na->snr);
    TEST_ASSERT_EQUAL_UINT32(111111, na->last_heard);
    TEST_ASSERT_EQUAL_UINT8(2, na->channel);
    TEST_ASSERT_TRUE(na->has_hops_away);
    TEST_ASSERT_EQUAL_UINT8(3, na->hops_away);
    TEST_ASSERT_EQUAL_UINT8(0xAB, na->next_hop);
    TEST_ASSERT_TRUE(nodeInfoLiteHasUser(na));
    TEST_ASSERT_FALSE(nodeInfoLiteViaMqtt(na));
    TEST_ASSERT_FALSE(nodeInfoLiteIsFavorite(na));
    TEST_ASSERT_FALSE(nodeInfoLiteIsIgnored(na));
    TEST_ASSERT_FALSE(nodeInfoLiteIsLicensed(na));

    // Satellite routing: position and device_metrics land in the maps, not the header.
#if !MESHTASTIC_EXCLUDE_POSITIONDB
    meshtastic_PositionLite pos;
    TEST_ASSERT_TRUE(db->copyNodePosition(0xA1000001, pos));
    TEST_ASSERT_EQUAL_INT32(375000000, pos.latitude_i);
    TEST_ASSERT_EQUAL_INT32(-1219876543, pos.longitude_i);
    TEST_ASSERT_EQUAL_INT32(123, pos.altitude);
    TEST_ASSERT_EQUAL_UINT32(1700000000, pos.time);
    TEST_ASSERT_EQUAL(meshtastic_Position_LocSource_LOC_INTERNAL, pos.location_source);
    TEST_ASSERT_EQUAL_UINT32(32, pos.precision_bits);
#endif
#if !MESHTASTIC_EXCLUDE_TELEMETRYDB
    meshtastic_DeviceMetrics dm;
    TEST_ASSERT_TRUE(db->copyNodeTelemetry(0xA1000001, dm));
    TEST_ASSERT_TRUE(dm.has_battery_level);
    TEST_ASSERT_EQUAL_UINT32(87, dm.battery_level);
    TEST_ASSERT_TRUE(dm.has_voltage);
    TEST_ASSERT_EQUAL_FLOAT(3.7f, dm.voltage);
#endif

    const meshtastic_NodeInfoLite *nb = db->getMeshNode(0xA1000002);
    TEST_ASSERT_NOT_NULL(nb);
    TEST_ASSERT_TRUE(nodeInfoLiteViaMqtt(nb));
    TEST_ASSERT_TRUE(nodeInfoLiteIsFavorite(nb));
    TEST_ASSERT_FALSE(nodeInfoLiteIsIgnored(nb));

    const meshtastic_NodeInfoLite *nc = db->getMeshNode(0xA1000003);
    TEST_ASSERT_NOT_NULL(nc);
    TEST_ASSERT_TRUE(nodeInfoLiteIsIgnored(nc));
    TEST_ASSERT_TRUE(nodeInfoLiteIsLicensed(nc));
    TEST_ASSERT_FALSE(nodeInfoLiteViaMqtt(nc));

    const meshtastic_NodeInfoLite *nd = db->getMeshNode(0xA1000004);
    TEST_ASSERT_NOT_NULL(nd);
    TEST_ASSERT_TRUE(nodeInfoLiteHasIsUnmessagable(nd));
    TEST_ASSERT_TRUE(nodeInfoLiteIsUnmessagable(nd));

    const meshtastic_NodeInfoLite *ne = db->getMeshNode(0xA1000005);
    TEST_ASSERT_NOT_NULL(ne);
    TEST_ASSERT_FALSE(nodeInfoLiteHasIsUnmessagable(ne));
    TEST_ASSERT_FALSE(nodeInfoLiteIsUnmessagable(ne));
    TEST_ASSERT_EQUAL(0, ne->public_key.size);

    // public_key survives byte-identical, and the public lookup API finds it.
    TEST_ASSERT_EQUAL(32, na->public_key.size);
    meshtastic_UserLite_public_key_t expected;
    fillKey(expected, 0x42);
    TEST_ASSERT_EQUAL_MEMORY(expected.bytes, na->public_key.bytes, 32);
    meshtastic_NodeInfoLite_public_key_t got = {0, {0}};
    TEST_ASSERT_TRUE(db->copyPublicKey(0xA1000001, got));
    TEST_ASSERT_EQUAL(32, got.size);
    TEST_ASSERT_EQUAL_MEMORY(expected.bytes, got.bytes, 32);
}

// has_position=false / has_device_metrics=false entries must not seed
// zero-position ghosts in the satellite maps.
// v24 assigned bits 0..10 of the bitfield; this build reads bit 11 as "heard over RF" and bits 12..23
// as the slot it was heard on. A legacy record carrying anything up there must not arrive claiming to
// have been heard, or a never-heard node reads as reachable whenever the stray slot matches ours.
static void test_v24StrayHighBits_doNotBecomeRfHearState(void)
{
    auto n = makeLegacyNode(0xD4000001, 1000);
    giveLegacyUser(n, "Stray", "ST");
    n.is_favorite = true;     // a real bit 3, which must survive
    n.bitfield = 0xFFFFFFFFu; // every reserved bit above 10 set
    writeLegacyNodesFile(24, {n});
    coldBoot();

    const meshtastic_NodeInfoLite *m = db->getMeshNode(0xD4000001);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_FALSE(nodeInfoLiteHasRfHear(m));
    TEST_ASSERT_EQUAL_UINT16(0, nodeInfoLiteHeardSlot(m));
    TEST_ASSERT_TRUE(nodeInfoLiteIsFavorite(m)); // the legacy bits it did own are untouched
}

static void test_absentSubmessages_noSatelliteGhostRows(void)
{
    auto a = makeLegacyNode(0xC3000001, 1000);
    giveLegacyUser(a, "NoPos", "NP");
    auto b = makeLegacyNode(0xC3000002, 2000);
    giveLegacyUser(b, "NoTel", "NT");
    writeLegacyNodesFile(24, {a, b});
    coldBoot();

    TEST_ASSERT_NOT_NULL(db->getMeshNode(0xC3000001));
    TEST_ASSERT_NOT_NULL(db->getMeshNode(0xC3000002));
#if !MESHTASTIC_EXCLUDE_POSITIONDB
    TEST_ASSERT_FALSE(db->hasNodePosition(0xC3000001));
    TEST_ASSERT_FALSE(db->hasNodePosition(0xC3000002));
    TEST_ASSERT_TRUE(db->snapshotPositionNodeNums(0).empty());
#endif
#if !MESHTASTIC_EXCLUDE_TELEMETRYDB
    TEST_ASSERT_FALSE(db->hasNodeTelemetry(0xC3000001));
    TEST_ASSERT_TRUE(db->snapshotTelemetryNodeNums(0).empty());
#endif
}

// --- sanitizeUtf8 firewall (hostile v24 names) ---

// The truncation firewall: a wide-but-VALID v24 long_name (UserLite allows 40
// bytes) whose 25-byte slim copy cuts a multi-byte sequence in half. Without
// migration's sanitizeUtf8, the orphaned lead byte makes the next
// saveNodeDatabaseToDisk() fail its PB_VALIDATE_UTF8 encode - and a failed
// save is what triggers saveToDisk()'s fsFormat() wipe on device.
static void test_truncatedWideName_sanitizedAndReencodable(void)
{
    // 23 ASCII bytes then Euro signs straddling the 24-byte truncation boundary.
    std::string straddle(23, 'a');
    straddle += "\xE2\x82\xAC\xE2\x82\xAC"; // two Euro signs, 29 bytes total - valid UTF-8 in v24
    auto s = makeLegacyNode(0xB2000002, 2000);
    giveLegacyUser(s, straddle.c_str(), "OK");

    writeLegacyNodesFile(24, {s});
    coldBoot();

    const meshtastic_NodeInfoLite *ns = db->getMeshNode(0xB2000002);
    TEST_ASSERT_NOT_NULL(ns);
    std::string expected(23, 'a');
    expected += '?'; // orphaned 0xE2 lead byte after the cut, replaced by sanitizeUtf8
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), ns->long_name);
    assertValidUtf8(ns->long_name, sizeof(ns->long_name));

    // The firewall itself: the migrated store must encode and re-decode.
    assertBootKeygenRan();
    TEST_ASSERT_TRUE_MESSAGE(db->saveDatabase(), "sanitized store must re-encode without a nanopb failure");
    meshtastic_NodeDatabase reloaded{};
    TEST_ASSERT_EQUAL(LoadFileResult::LOAD_SUCCESS,
                      db->loadProto(nodeDatabaseFileName, db->getMaxNodesAllocatedSize(), sizeof(meshtastic_NodeDatabase),
                                    &meshtastic_NodeDatabase_msg, &reloaded));
    TEST_ASSERT_EQUAL_UINT32(DEVICESTATE_CUR_VER, reloaded.version);
}

// Raw invalid UTF-8 inside a v24 name (written by pre-PB_VALIDATE_UTF8
// firmware): nanopb refuses to decode that node and the legacy callback drops
// it, but the rest of the file must still migrate and the boot must still
// complete and re-save. One poisoned node must never cost the whole database.
static void test_rawInvalidUtf8Node_droppedWithoutBreakingMigration(void)
{
    static const char kPlaceholderLong[] = "Bad0(nameXXzzYY"; // 15 ASCII bytes, patched below
    static const char kHostileLong[] = "Bad\xC3"
                                       "(name\xFF\xFE"
                                       "zz\xE2\x82"; // invalid leads + truncated tail, same 15 bytes

    auto h = makeLegacyNode(0xB2000001, 1000);
    giveLegacyUser(h, kPlaceholderLong, "HN");

    auto good = makeLegacyNode(0xB2000003, 3000);
    giveLegacyUser(good, "Good Node", "GN");

    std::vector<uint8_t> buf = encodeLegacyNodes(24, {h, good});
    patchBytes(buf, kPlaceholderLong, kHostileLong, 15);
    writeNodesBytes(buf.data(), buf.size());
    coldBoot();

    // The poisoned node is gone (its num was consumed before the failing name,
    // so no partial-decode fragment can carry it either)...
    TEST_ASSERT_NULL(db->getMeshNode(0xB2000001));
    // ...while its well-formed sibling in the same file migrated intact.
    const meshtastic_NodeInfoLite *ng = db->getMeshNode(0xB2000003);
    TEST_ASSERT_NOT_NULL(ng);
    TEST_ASSERT_EQUAL_STRING("Good Node", ng->long_name);
    TEST_ASSERT_EQUAL_UINT32(DEVICESTATE_CUR_VER, nodeDatabase.version);

    // And the migrated store still persists cleanly.
    assertBootKeygenRan();
    TEST_ASSERT_TRUE(db->saveDatabase());
    meshtastic_NodeDatabase reloaded{};
    TEST_ASSERT_EQUAL(LoadFileResult::LOAD_SUCCESS,
                      db->loadProto(nodeDatabaseFileName, db->getMaxNodesAllocatedSize(), sizeof(meshtastic_NodeDatabase),
                                    &meshtastic_NodeDatabase_msg, &reloaded));
    TEST_ASSERT_EQUAL_UINT32(DEVICESTATE_CUR_VER, reloaded.version);
}

// --- Capacity ---

// A legacy file from a larger-cap build migrates at most MAX_NUM_NODES entries
// in file order; no OOB under ASan (the getOrCreate boot-loop family guard).
static void test_overCapLegacyFile_truncatesToMaxNumNodes(void)
{
    const int maxNodes = MAX_NUM_NODES;
    const int extra = 20;
    std::vector<meshtastic_NodeInfoLite_Legacy> nodes;
    nodes.reserve(maxNodes + extra);
    for (int i = 0; i < maxNodes + extra; i++) {
        auto n = makeLegacyNode(0xE5000000u + i, (uint32_t)(i + 1)); // ascending: index 0 is oldest
        char ln[16], sn[5];
        snprintf(ln, sizeof(ln), "n%d", i);
        snprintf(sn, sizeof(sn), "%02d", i % 100);
        giveLegacyUser(n, ln, sn); // users required: keyless/userless entries are purged by cleanupMeshDB
        nodes.push_back(n);
    }
    writeLegacyNodesFile(24, nodes);
    coldBoot();

    // Exactly the hot cap: file entries 0..max-1 migrated, the tail dropped,
    // then nodeDBSelfCare evicted one old migrated node to admit self. Which
    // of the oldest is the victim is an eviction-policy detail; only the
    // counts and the cap boundary are contract here.
    TEST_ASSERT_EQUAL_INT(maxNodes, (int)db->getNumMeshNodes());
    TEST_ASSERT_NULL(db->getMeshNode(0xE5000000u + maxNodes));             // first beyond the cap: dropped
    TEST_ASSERT_NULL(db->getMeshNode(0xE5000000u + maxNodes + extra - 1)); // last beyond the cap: dropped
    TEST_ASSERT_NOT_NULL(db->getMeshNode(0xE5000000u + maxNodes - 1));     // last within the cap: kept
    TEST_ASSERT_NOT_NULL(db->getMeshNode(db->getNodeNum()));               // self admitted
    int survivors = 0;
    for (int i = 0; i < maxNodes; i++) {
        if (db->getMeshNode(0xE5000000u + i))
            survivors++;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(maxNodes - 1, survivors, "exactly one within-cap node should have been evicted for self");
}

// --- Full boot ladder persistence ---

// The deferred migrationSavePending re-save must land: after the boot,
// the on-disk nodes.proto is v25 with the migrated node, key, and satellite.
static void test_fullBootLadder_persistsMigratedV25(void)
{
    auto a = makeLegacyNode(0xD4000001, 4000);
    giveLegacyUser(a, "Persist Me", "PM");
    fillKey(a.user.public_key, 0x77);
    a.has_position = true;
    a.position.latitude_i = 101010101;
    a.position.longitude_i = -202020202;
    writeLegacyNodesFile(24, {a});
    coldBoot();

    assertBootKeygenRan();

    meshtastic_NodeDatabase reloaded{};
    TEST_ASSERT_EQUAL(LoadFileResult::LOAD_SUCCESS,
                      db->loadProto(nodeDatabaseFileName, db->getMaxNodesAllocatedSize(), sizeof(meshtastic_NodeDatabase),
                                    &meshtastic_NodeDatabase_msg, &reloaded));
    TEST_ASSERT_EQUAL_UINT32(DEVICESTATE_CUR_VER, reloaded.version);

    const meshtastic_NodeInfoLite *persisted = nullptr;
    for (const auto &n : reloaded.nodes) {
        if (n.num == 0xD4000001)
            persisted = &n;
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(persisted, "migrated node must survive the v25 re-save");
    TEST_ASSERT_EQUAL_STRING("Persist Me", persisted->long_name);
    TEST_ASSERT_TRUE(persisted->bitfield & NODEINFO_BITFIELD_HAS_USER_MASK);
    TEST_ASSERT_EQUAL(32, persisted->public_key.size);
    meshtastic_UserLite_public_key_t expected;
    fillKey(expected, 0x77);
    TEST_ASSERT_EQUAL_MEMORY(expected.bytes, persisted->public_key.bytes, 32);

#if !MESHTASTIC_EXCLUDE_POSITIONDB
    // With the decode targets disarmed (steady state), satellite entries land in
    // the struct's own vectors - so this asserts the on-disk projection directly.
    bool posFound = false;
    for (const auto &e : reloaded.positions) {
        if (e.num == 0xD4000001 && e.has_position) {
            posFound = true;
            TEST_ASSERT_EQUAL_INT32(101010101, e.position.latitude_i);
            TEST_ASSERT_EQUAL_INT32(-202020202, e.position.longitude_i);
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(posFound, "satellite position must survive the v25 re-save");
#endif
}

// --- module.proto: the broadcast_message cut from 100 to 60 bytes ---

// A pre-cut save with a 100-byte beacon message fails the current decode. Regression guarded: that
// installed default module config, silently resetting MQTT, telemetry and every other module.
static void test_preCutModuleConfig_longBeaconMessage_migratesAndKeepsOtherModules(void)
{
    const meshtastic_LocalModuleConfig base = distinctiveModuleConfig();
    const std::vector<uint8_t> file = encodePreCutModuleConfig(base, std::string(100, 'x'));
    writeModuleBytes(file.data(), file.size());
    coldBoot();

    assertDistinctiveModulesSurvived(moduleConfig);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(std::string(60, 'x').c_str(), moduleConfig.mesh_beacon.broadcast_message,
                                     "the message keeps its first 60 bytes");

    meshtastic_LocalModuleConfig reloaded = meshtastic_LocalModuleConfig_init_zero;
    TEST_ASSERT_EQUAL_MESSAGE(LoadFileResult::LOAD_SUCCESS,
                              db->loadProto(moduleConfigFileName, meshtastic_LocalModuleConfig_size,
                                            sizeof(meshtastic_LocalModuleConfig), &meshtastic_LocalModuleConfig_msg, &reloaded),
                              "the migrated config is saved in the current shape, so the next boot decodes it");
    assertDistinctiveModulesSurvived(reloaded);
    TEST_ASSERT_EQUAL_UINT(60, strlen(reloaded.mesh_beacon.broadcast_message));
}

// A pre-cut save can be larger than the current encoded maximum, which the first read stops at.
static void test_preCutModuleConfig_largerThanTheCurrentMaximum_stillMigrates(void)
{
    const meshtastic_LocalModuleConfig base = distinctiveModuleConfig();
    const std::vector<uint8_t> file = encodePreCutModuleConfig(base, std::string(100, 'x'), 1040);
    TEST_ASSERT_GREATER_THAN_UINT(meshtastic_LocalModuleConfig_size, file.size());
    writeModuleBytes(file.data(), file.size());
    coldBoot();

    assertDistinctiveModulesSurvived(moduleConfig);
    TEST_ASSERT_EQUAL_UINT(60, strlen(moduleConfig.mesh_beacon.broadcast_message));
}

// The cut never splits a character: a 2-byte character straddling byte 60 is dropped whole.
static void test_truncateLegacyBeaconMessage_straddlingCharacter_isDroppedWhole(void)
{
    const std::string message = std::string(59, 'a') + "\xC3\xA9" + std::string(39, 'b');
    const std::vector<uint8_t> in = encodePreCutModuleConfig(distinctiveModuleConfig(), message);
    std::vector<uint8_t> out(in.size());
    size_t outLen = 0;
    TEST_ASSERT_TRUE(truncateLegacyBeaconMessage(in.data(), in.size(), out.data(), out.size(), outLen));

    meshtastic_LocalModuleConfig decoded = meshtastic_LocalModuleConfig_init_zero;
    TEST_ASSERT_TRUE(pb_decode_from_bytes(out.data(), outLen, &meshtastic_LocalModuleConfig_msg, &decoded));
    TEST_ASSERT_EQUAL_STRING(std::string(59, 'a').c_str(), decoded.mesh_beacon.broadcast_message);
}

// A message that already fits needs no cut, so the rewrite declines and a normal decode reads the file.
static void test_truncateLegacyBeaconMessage_messageThatFits_needsNoMigration(void)
{
    const std::vector<uint8_t> in = encodePreCutModuleConfig(distinctiveModuleConfig(), std::string(60, 'x'));
    std::vector<uint8_t> out(in.size());
    size_t outLen = 0;
    TEST_ASSERT_FALSE(truncateLegacyBeaconMessage(in.data(), in.size(), out.data(), out.size(), outLen));

    meshtastic_LocalModuleConfig decoded = meshtastic_LocalModuleConfig_init_zero;
    TEST_ASSERT_TRUE(pb_decode_from_bytes(in.data(), in.size(), &meshtastic_LocalModuleConfig_msg, &decoded));
    TEST_ASSERT_EQUAL_UINT(60, strlen(decoded.mesh_beacon.broadcast_message));
}

// A long message in a file that is broken elsewhere is not a pre-cut save: it still gets defaults.
static void test_preCutModuleConfig_corruptElsewhere_installsDefaults(void)
{
    std::vector<uint8_t> file = encodePreCutModuleConfig(distinctiveModuleConfig(), std::string(100, 'x'));
    file.insert(file.end(), {0xFF, 0xFF, 0xFF}); // a key with no end
    std::vector<uint8_t> out(file.size());
    size_t outLen = 0;
    TEST_ASSERT_FALSE(truncateLegacyBeaconMessage(file.data(), file.size(), out.data(), out.size(), outLen));

    writeModuleBytes(file.data(), file.size());
    coldBoot();
    TEST_ASSERT_TRUE_MESSAGE(strcmp("mqtt.example", moduleConfig.mqtt.address) != 0, "a broken file is not migrated");
}

NDBM_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    // First boot on the empty sandbox: installs defaults, runs keygen, and
    // persists the base config files every later cold boot reloads.
    coldBoot();

    UNITY_BEGIN();

    printf("\n=== Version-gate ladder ===\n");
    RUN_TEST(test_emptyV24File_migratesToEmptyV25);
    RUN_TEST(test_versionBelowMin_discardsToDefaults);
    RUN_TEST(test_garbageNodesProto_installsDefaults);

    printf("\n=== Migration fidelity ===\n");
    RUN_TEST(test_v24RoundTrip_migratesFieldsBitfieldAndSatellites);
    RUN_TEST(test_v24StrayHighBits_doNotBecomeRfHearState);
    RUN_TEST(test_absentSubmessages_noSatelliteGhostRows);

    printf("\n=== sanitizeUtf8 firewall ===\n");
    RUN_TEST(test_truncatedWideName_sanitizedAndReencodable);
    RUN_TEST(test_rawInvalidUtf8Node_droppedWithoutBreakingMigration);

    printf("\n=== Capacity and persistence ===\n");
    RUN_TEST(test_overCapLegacyFile_truncatesToMaxNumNodes);
    RUN_TEST(test_fullBootLadder_persistsMigratedV25);

    printf("\n=== module.proto beacon message cut ===\n");
    RUN_TEST(test_preCutModuleConfig_longBeaconMessage_migratesAndKeepsOtherModules);
    RUN_TEST(test_preCutModuleConfig_largerThanTheCurrentMaximum_stillMigrates);
    RUN_TEST(test_truncateLegacyBeaconMessage_straddlingCharacter_isDroppedWhole);
    RUN_TEST(test_truncateLegacyBeaconMessage_messageThatFits_needsNoMigration);
    RUN_TEST(test_preCutModuleConfig_corruptElsewhere_installsDefaults);

    exit(UNITY_END());
}
NDBM_TEST_ENTRY void loop() {}

#else // !FSCom - no filesystem, nothing to migrate

void setUp(void) {}
void tearDown(void) {}

NDBM_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}
NDBM_TEST_ENTRY void loop() {}

#endif
