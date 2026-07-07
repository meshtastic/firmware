// Adversarial fuzzing of the packet path - the decrypt/decode/dispatch pipeline that a crafted RF
// packet flows through, plus the phone-forward validation gate.
//
// Unlike test/test_fuzz_decode (pure functions, no fixture) these drive real firmware machinery:
// Router::perhapsDecode over a configured channel, module handlers, the traceroute route-processing
// functions, and MeshService's phone-delivery gate. It reuses the MockNodeDB + channel bring-up from
// test/test_packet_signing. Runs under the `coverage` env (AddressSanitizer + LeakSanitizer); any
// out-of-bounds access or leak on adversarial input turns the run RED. Inputs are driven by a seeded
// LCG so failures reproduce from the printed seed.
//
//   Group E1  encrypted RX -> perhapsDecode over arbitrary ciphertext
//   Group E2  NodeInfoModule handler over arbitrary User structs
//   Group E3  TraceRoute route-processing over adversarial RouteDiscovery (route/snr count edges)
//   Group E4  MeshService phone-forward gate (nested-string validation)
//   Group E5  AdminModule dispatch over adversarial AdminMessage (local from==0, setter/node-op tags)
//   Group E6  MeshBeaconListenerModule handler over adversarial MeshBeacon (un-terminated message, offer PSK)
//   Group E7  NodeDB::updateUser / updateFrom over adversarial nodeId + User / MeshPacket
//   Group E8  PositionModule handler over adversarial Position (self/remote origin, fixed_position, RTC path)
//   Group E9  DeviceTelemetryModule handler over adversarial Telemetry (all variant tags)
//   Group E10 NeighborInfoModule handler over adversarial NeighborInfo (neighbor-count edges)
//
// KeyVerificationModule (handler inert until a prior handshake advances its private state) and
// StoreForwardModule (needs PSRAM + router wiring) are instead covered at the decode level in test_fuzz_decode.

#include "MeshTypes.h" // include BEFORE TestUtil.h
#include "TestUtil.h"
#include <unity.h>

#if !(MESHTASTIC_EXCLUDE_PKI)

#include "mesh-pb-constants.h"
#include "mesh/Channels.h"
#include "mesh/CryptoEngine.h"
#include "mesh/MeshService.h"
#include "mesh/NodeDB.h"
#include "mesh/Router.h"
#include "modules/AdminModule.h"
#include "modules/NeighborInfoModule.h"
#include "modules/NodeInfoModule.h"
#include "modules/PositionModule.h"
#include "modules/Telemetry/DeviceTelemetry.h"
#include "modules/TraceRouteModule.h"
#include <cstdio>
#include <cstring>
#include <pb_decode.h>
#include <vector>

#if !MESHTASTIC_EXCLUDE_BEACON
#include "modules/MeshBeaconModule.h"
#endif

static constexpr NodeNum LOCAL_NODE = 0x0A0A0A0A;
static constexpr NodeNum REMOTE_NODE = 0x0B0B0B0B;

// Deterministic RNG (rngSeed/rngNext/rngByte/rngRange/rngFill/rngEdgeNodeNum) - shared seeded LCG.
#include "support/AdminModuleTestShim.h"
#include "support/DeterministicRng.h"
#include "support/MockMeshService.h"
static constexpr uint64_t BASE_SEED = 0x00BADF00DULL;

// ---------------------------------------------------------------------------
// MockNodeDB - mirror of test/test_packet_signing so we can inject nodes.
// ---------------------------------------------------------------------------
class MockNodeDB : public NodeDB
{
  public:
    void clearTestNodes()
    {
        testNodes.clear();
        meshNodes = &testNodes;
        numMeshNodes = 0;
    }
    void addNode(NodeNum num)
    {
        meshtastic_NodeInfoLite node = meshtastic_NodeInfoLite_init_zero;
        node.num = num;
        testNodes.push_back(node);
        meshNodes = &testNodes;
        numMeshNodes = testNodes.size();
    }
    void setPublicKey(NodeNum num, const uint8_t *pubKey)
    {
        meshtastic_NodeInfoLite *n = getMeshNode(num);
        TEST_ASSERT_NOT_NULL(n);
        n->public_key.size = 32;
        memcpy(n->public_key.bytes, pubKey, 32);
    }
    void setSignerBit(NodeNum num, bool value)
    {
        meshtastic_NodeInfoLite *n = getMeshNode(num);
        TEST_ASSERT_NOT_NULL(n);
        nodeInfoLiteSetBit(n, NODEINFO_BITFIELD_HAS_XEDDSA_SIGNED_MASK, value);
    }
    std::vector<meshtastic_NodeInfoLite> testNodes;
};

static MockNodeDB *mockNodeDB = nullptr;

static MockMeshService *mockService = nullptr;

void setUp(void)
{
    config = meshtastic_LocalConfig_init_zero;
    moduleConfig = meshtastic_LocalModuleConfig_init_zero;
    owner = meshtastic_User_init_zero;

    mockNodeDB = new MockNodeDB();
    mockNodeDB->clearTestNodes();
    nodeDB = mockNodeDB;
    myNodeInfo.my_node_num = LOCAL_NODE;

    mockService = new MockMeshService();
    service = mockService;

    channels.initDefaults();
    channels.onConfigChanged();
}

void tearDown(void)
{
    delete mockNodeDB;
    mockNodeDB = nullptr;
    nodeDB = nullptr;

    service = nullptr;
    delete mockService;
    mockService = nullptr;
}

// ===========================================================================
// Group E1 - encrypted RX -> perhapsDecode over arbitrary ciphertext
// ===========================================================================
void test_E1_perhaps_decode_fuzz(void)
{
    printf("  seed=0x%llx\n", (unsigned long long)(BASE_SEED ^ 0xE1));
    rngSeed(BASE_SEED ^ 0xE1);

    // A known signer with a stored key so the verify/downgrade policy branches get exercised whenever
    // random ciphertext happens to decrypt into a plausible signed Data.
    uint8_t pub[32], priv[32];
    crypto->generateKeyPair(pub, priv);
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->setPublicKey(REMOTE_NODE, pub);
    mockNodeDB->setSignerBit(REMOTE_NODE, true);

    const NodeNum targets[] = {NODENUM_BROADCAST, LOCAL_NODE, REMOTE_NODE, 0x12345678};

    for (unsigned k = 0; k < 8000; k++) {
        meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
        p.from = REMOTE_NODE;
        p.to = targets[rngRange(4)];
        p.id = rngNext();
        p.channel = (uint8_t)rngByte(); // channel-hash hint; often no match -> decrypt tries each channel
        p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
        p.pki_encrypted = (rngRange(2) == 0);

        p.encrypted.size = rngRange(sizeof(p.encrypted.bytes) + 1); // 0..256, always in bounds
        rngFill(p.encrypted.bytes, p.encrypted.size);

        DecodeState st = perhapsDecode(&p);
        // Any verdict is fine; the contract is that arbitrary ciphertext never crashes the pipeline.
        TEST_ASSERT_TRUE(st == DECODE_SUCCESS || st == DECODE_FAILURE || st == DECODE_FATAL);
    }
}

// ===========================================================================
// Group E2 - NodeInfoModule handler over arbitrary User structs
// ===========================================================================
class NodeInfoTestShim : public NodeInfoModule
{
  public:
    using NodeInfoModule::handleReceivedProtobuf;
};

// Fill a User with adversarial content: char fields random, sometimes left un-terminated to test that
// downstream copies stay bounded; byte fields random-sized.
static meshtastic_User fuzzUser()
{
    meshtastic_User u = meshtastic_User_init_zero;
    auto fillStr = [](char *s, size_t cap) {
        size_t n = rngRange(cap + 4); // may exceed cap: exercises the un-terminated path
        for (size_t i = 0; i < cap; i++)
            s[i] = (i < n) ? (char)rngByte() : '\0';
        if (rngRange(2)) // half the time force a stray non-NUL in the last slot
            s[cap - 1] = (char)(0x80 + rngRange(0x80));
    };
    fillStr(u.id, sizeof(u.id));
    fillStr(u.long_name, sizeof(u.long_name));
    fillStr(u.short_name, sizeof(u.short_name));
    u.hw_model = (meshtastic_HardwareModel)rngRange(256);
    u.role = (meshtastic_Config_DeviceConfig_Role)rngRange(16);
    u.public_key.size = rngRange(33); // 0..32
    rngFill(u.public_key.bytes, u.public_key.size);
    return u;
}

void test_E2_nodeinfo_handler_fuzz(void)
{
    printf("  seed=0x%llx\n", (unsigned long long)(BASE_SEED ^ 0xE2));
    rngSeed(BASE_SEED ^ 0xE2);

    // Function-local static: NodeInfoModule derives from OSThread, whose ctor registers `this` in a
    // global ThreadController and whose dtor deregisters it. A stack local would be freed on return -
    // and Unity's TEST_* macros exit via longjmp, skipping C++ dtors entirely - leaving a dangling
    // pointer that a later OSThread ctor trips over (ASan stack-use-after-return). A static lives for
    // the whole process, so its registration stays valid regardless of longjmp.
    static NodeInfoTestShim shim;
    mockNodeDB->addNode(REMOTE_NODE); // non-signer: handler falls through to updateUser()

    for (unsigned k = 0; k < 6000; k++) {
        // Clear any key stored last iteration so updateUser() keeps reaching the string-copy path
        // (CopyUserToNodeInfoLite) instead of early-returning on a key mismatch.
        meshtastic_NodeInfoLite *rn = mockNodeDB->getMeshNode(REMOTE_NODE);
        if (rn)
            rn->public_key.size = 0;

        // Broadcast so the want_response / service-dependent reply path is skipped.
        meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
        mp.from = REMOTE_NODE;
        mp.to = NODENUM_BROADCAST;
        mp.id = rngNext();
        mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        mp.decoded.portnum = meshtastic_PortNum_NODEINFO_APP;
        mp.xeddsa_signed = (rngRange(2) == 0);

        meshtastic_User u = fuzzUser();
        // Contract: never crashes and never corrupts NodeDB regardless of the User contents.
        (void)shim.handleReceivedProtobuf(mp, &u);
    }
    // NodeDB survived the fuzzing intact (the node is still present, not corrupted away).
    TEST_ASSERT_NOT_NULL(mockNodeDB->getMeshNode(REMOTE_NODE));
}

// ===========================================================================
// Group E3 - TraceRoute route-processing over adversarial RouteDiscovery
// ===========================================================================
static const pb_size_t ROUTE_MAX = ROUTE_SIZE; // 8

// Build a RouteDiscovery with caller-chosen (bounded) counts, filled with arbitrary node numbers.
static meshtastic_RouteDiscovery makeRoute(pb_size_t rc, pb_size_t st, pb_size_t rbc, pb_size_t sb)
{
    meshtastic_RouteDiscovery r = meshtastic_RouteDiscovery_init_zero;
    r.route_count = rc;
    for (pb_size_t i = 0; i < rc; i++)
        r.route[i] = rngNext();
    r.snr_towards_count = st;
    for (pb_size_t i = 0; i < st; i++)
        r.snr_towards[i] = (int8_t)rngByte();
    r.route_back_count = rbc;
    for (pb_size_t i = 0; i < rbc; i++)
        r.route_back[i] = rngNext();
    r.snr_back_count = sb;
    for (pb_size_t i = 0; i < sb; i++)
        r.snr_back[i] = (int8_t)rngByte();
    return r;
}

void test_E3_traceroute_route_processing_fuzz(void)
{
    printf("  seed=0x%llx\n", (unsigned long long)(BASE_SEED ^ 0xE3));
    rngSeed(BASE_SEED ^ 0xE3);

    static TraceRouteModule tr; // static: OSThread-derived, see the note in test_E2 (ThreadController lifetime)

    for (unsigned k = 0; k < 6000; k++) {
        // Mostly valid RouteDiscovery encodings, with adversarial count combinations (incl. the
        // snr_back_count==0 / snr_towards_count>0 shape behind the printRoute guard fix); plus a
        // fraction of pure-random payloads that must fail decode cleanly.
        meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
        mp.from = (rngRange(2)) ? LOCAL_NODE : REMOTE_NODE; // origin==us drives the printRoute back-path
        mp.to = (rngRange(2)) ? LOCAL_NODE : REMOTE_NODE;
        mp.id = rngNext();
        mp.rx_snr = (float)((int)rngRange(40) - 20);
        mp.hop_start = (uint8_t)rngRange(8);
        mp.hop_limit = (uint8_t)rngRange(8);
        mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        mp.decoded.portnum = meshtastic_PortNum_TRACEROUTE_APP;
        mp.decoded.want_response = (rngRange(2) == 0);
        mp.decoded.request_id = (rngRange(2) == 0) ? 0 : rngNext();

        if (rngRange(5) == 0) {
            // Pure-random payload: processUpgradedPacket must reject it at decode and not crash.
            mp.decoded.payload.size = rngRange(sizeof(mp.decoded.payload.bytes) + 1);
            rngFill(mp.decoded.payload.bytes, mp.decoded.payload.size);
        } else {
            meshtastic_RouteDiscovery r =
                makeRoute(rngRange(ROUTE_MAX + 1), rngRange(ROUTE_MAX + 1), rngRange(ROUTE_MAX + 1), rngRange(ROUTE_MAX + 1));
            mp.decoded.payload.size = pb_encode_to_bytes(mp.decoded.payload.bytes, sizeof(mp.decoded.payload.bytes),
                                                         &meshtastic_RouteDiscovery_msg, &r);
        }

        tr.processUpgradedPacket(mp); // decode -> alterReceivedProtobuf (insert/append/printRoute)

        // After processing, the re-encoded route arrays must still be within bounds.
        meshtastic_RouteDiscovery out = meshtastic_RouteDiscovery_init_zero;
        if (pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_RouteDiscovery_msg, &out)) {
            TEST_ASSERT_TRUE_MESSAGE(out.route_count <= ROUTE_MAX, "route_count overran ROUTE_SIZE");
            TEST_ASSERT_TRUE_MESSAGE(out.route_back_count <= ROUTE_MAX, "route_back_count overran ROUTE_SIZE");
            TEST_ASSERT_TRUE_MESSAGE(out.snr_towards_count <= ROUTE_MAX, "snr_towards_count overran ROUTE_SIZE");
            TEST_ASSERT_TRUE_MESSAGE(out.snr_back_count <= ROUTE_MAX, "snr_back_count overran ROUTE_SIZE");
        }
    }
}

// ===========================================================================
// Group E4 - MeshService phone-forward gate
// ===========================================================================
static meshtastic_Data makeData(meshtastic_PortNum port, const uint8_t *bytes, size_t n)
{
    meshtastic_Data d = meshtastic_Data_init_zero;
    TEST_ASSERT_TRUE_MESSAGE(n <= sizeof(d.payload.bytes), "payload exceeds meshtastic_Data capacity");
    if (n > 0)
        TEST_ASSERT_NOT_NULL(bytes);
    d.portnum = port;
    d.payload.size = n;
    for (size_t i = 0; i < n; i++)
        d.payload.bytes[i] = bytes[i];
    return d;
}

void test_E4_phone_forward_gate(void)
{
    // Waypoint.name is field 6 (tag 0x32); User.long_name is field 2 (tag 0x12); both STRING, so
    // PB_VALIDATE_UTF8 rejects invalid UTF-8 content on decode.
    const uint8_t badWaypoint[] = {0x32, 0x01, 0xFF};      // name = single invalid lead byte
    const uint8_t goodWaypoint[] = {0x32, 0x02, 'O', 'K'}; // name = "OK"
    const uint8_t badUser[] = {0x12, 0x01, 0xFF};          // long_name = invalid
    const uint8_t goodUser[] = {0x12, 0x02, 'O', 'K'};     // long_name = "OK"
    const uint8_t arbitrary[] = {0xFF, 0xFE, 0x00, 0x80};

    meshtastic_Data d;

    d = makeData(meshtastic_PortNum_WAYPOINT_APP, badWaypoint, sizeof(badWaypoint));
    TEST_ASSERT_FALSE_MESSAGE(MeshService::phonePayloadIsDecodable(d), "invalid-UTF8 waypoint must be withheld");

    d = makeData(meshtastic_PortNum_WAYPOINT_APP, goodWaypoint, sizeof(goodWaypoint));
    TEST_ASSERT_TRUE_MESSAGE(MeshService::phonePayloadIsDecodable(d), "valid waypoint must pass");

    d = makeData(meshtastic_PortNum_NODEINFO_APP, badUser, sizeof(badUser));
    TEST_ASSERT_FALSE_MESSAGE(MeshService::phonePayloadIsDecodable(d), "invalid-UTF8 nodeinfo must be withheld");

    d = makeData(meshtastic_PortNum_NODEINFO_APP, goodUser, sizeof(goodUser));
    TEST_ASSERT_TRUE_MESSAGE(MeshService::phonePayloadIsDecodable(d), "valid nodeinfo must pass");

    // Empty payloads decode to all-defaults -> pass.
    d = makeData(meshtastic_PortNum_WAYPOINT_APP, nullptr, 0);
    TEST_ASSERT_TRUE(MeshService::phonePayloadIsDecodable(d));

    // Non-gated portnums always pass through, even with arbitrary bytes (text is opaque `bytes`).
    d = makeData(meshtastic_PortNum_TEXT_MESSAGE_APP, arbitrary, sizeof(arbitrary));
    TEST_ASSERT_TRUE_MESSAGE(MeshService::phonePayloadIsDecodable(d), "text payload must not be gated");

    // Fuzz: for gated portnums the gate verdict must exactly match a raw decode, and never crash.
    rngSeed(BASE_SEED ^ 0xE4);
    for (unsigned k = 0; k < 8000; k++) {
        uint8_t buf[64];
        size_t n = rngRange(sizeof(buf) + 1);
        rngFill(buf, n);
        meshtastic_PortNum port = (rngRange(2)) ? meshtastic_PortNum_WAYPOINT_APP : meshtastic_PortNum_NODEINFO_APP;
        d = makeData(port, buf, n);

        bool gate = MeshService::phonePayloadIsDecodable(d);
        bool raw;
        if (port == meshtastic_PortNum_WAYPOINT_APP) {
            meshtastic_Waypoint w = meshtastic_Waypoint_init_zero;
            raw = pb_decode_from_bytes(d.payload.bytes, d.payload.size, &meshtastic_Waypoint_msg, &w);
        } else {
            meshtastic_User u = meshtastic_User_init_zero;
            raw = pb_decode_from_bytes(d.payload.bytes, d.payload.size, &meshtastic_User_msg, &u);
        }
        TEST_ASSERT_EQUAL_MESSAGE(raw, gate, "gate verdict must match nested decode");
    }
}

// ===========================================================================
// Group E5 - AdminModule dispatch over adversarial AdminMessage
// (AdminModuleTestShim comes from test/support - the friend seam AdminModule.h declares.)
// ===========================================================================
// A scoped subset of setter / node-list tags - the ones reachable without a fully-booted module graph.
// EXCLUDES: side-effecting tags even with saves deferred (reboot*, shutdown, *factory_reset*,
// nodedb_reset, ota, enter_dfu, delete_file, *_preferences, exit_simulator, begin/commit_edit_settings);
// get_* reply builders (un-mocked service paths); set_owner (needs global nodeInfoModule) and
// set_fixed_position (needs global positionModule) - their payloads are covered by E2/E7 and
// test_fuzz_decode. set_config and set_channel ARE fuzzed - they found two crashes (a SIGFPE in LoRa
// validation and unbounded getKey recursion), both fixed; E5 re-fuzzes the triggers as regression
// guards. The module-config variant is constrained below only to skip the beacon variant's global.
static const pb_size_t SAFE_ADMIN_TAGS[] = {
    meshtastic_AdminMessage_set_config_tag,
    meshtastic_AdminMessage_set_module_config_tag,
    meshtastic_AdminMessage_set_channel_tag,
    meshtastic_AdminMessage_set_favorite_node_tag,
    meshtastic_AdminMessage_remove_favorite_node_tag,
    meshtastic_AdminMessage_set_ignored_node_tag,
    meshtastic_AdminMessage_remove_ignored_node_tag,
    meshtastic_AdminMessage_toggle_muted_node_tag,
    meshtastic_AdminMessage_remove_by_nodenum_tag,
    meshtastic_AdminMessage_add_contact_tag,
    meshtastic_AdminMessage_remove_fixed_position_tag,
    meshtastic_AdminMessage_set_time_only_tag,
};
static const size_t NUM_SAFE_ADMIN_TAGS = sizeof(SAFE_ADMIN_TAGS) / sizeof(SAFE_ADMIN_TAGS[0]);

// ModuleConfig variants that handleSetModuleConfig applies with only in-RAM copy + (deferred) save.
// Excludes mesh_beacon, whose handler derefs the global meshBeaconBroadcastModule.
static const pb_size_t SAFE_MODULE_CONFIG_TAGS[] = {
    meshtastic_ModuleConfig_mqtt_tag,
    meshtastic_ModuleConfig_serial_tag,
    meshtastic_ModuleConfig_external_notification_tag,
    meshtastic_ModuleConfig_store_forward_tag,
    meshtastic_ModuleConfig_range_test_tag,
    meshtastic_ModuleConfig_telemetry_tag,
    meshtastic_ModuleConfig_canned_message_tag,
    meshtastic_ModuleConfig_audio_tag,
    meshtastic_ModuleConfig_remote_hardware_tag,
    meshtastic_ModuleConfig_neighbor_info_tag,
    meshtastic_ModuleConfig_ambient_lighting_tag,
    meshtastic_ModuleConfig_detection_sensor_tag,
    meshtastic_ModuleConfig_paxcounter_tag,
};
static const size_t NUM_SAFE_MODULE_CONFIG_TAGS = sizeof(SAFE_MODULE_CONFIG_TAGS) / sizeof(SAFE_MODULE_CONFIG_TAGS[0]);

// A node number spanning the shared edge pool (0/1/broadcast + self) or broad random.
static NodeNum fuzzNodeNum()
{
    return (rngRange(4) == 0) ? rngNext() : rngEdgeNodeNum(&LOCAL_NODE, 1);
}

// Randomize a ChannelSettings name + PSK (shared by the set_channel case and fuzzBeacon). Name is
// random-length but NUL-terminated (nanopb terminates decoded strings, so un-terminated isn't
// wire-reachable); PSK is 0..32 bytes including empty.
static void fuzzChannelSettings(meshtastic_ChannelSettings &s)
{
    size_t nameLen = rngRange(sizeof(s.name));
    for (size_t i = 0; i < sizeof(s.name); i++)
        s.name[i] = (i < nameLen) ? (char)(1 + rngRange(255)) : '\0';
    s.psk.size = rngRange(sizeof(s.psk.bytes) + 1);
    rngFill(s.psk.bytes, s.psk.size);
}

// Build a wire-plausible-but-adversarial AdminMessage: size-bearing fields (pubkey, psk) stay within
// capacity (as nanopb would enforce on decode) so we find real logic bugs, not fabricated OOB that
// can't arrive over the air.
static meshtastic_AdminMessage fuzzAdminMessage()
{
    meshtastic_AdminMessage r = meshtastic_AdminMessage_init_zero;
    r.which_payload_variant = SAFE_ADMIN_TAGS[rngRange(NUM_SAFE_ADMIN_TAGS)];
    switch (r.which_payload_variant) {
    case meshtastic_AdminMessage_set_config_tag:
        // LoRa only (position variant would deref global gps). use_preset fuzzed both ways, incl. the
        // manual bandwidth==0 path that used to SIGFPE the validator.
        r.set_config.which_payload_variant = meshtastic_Config_lora_tag;
        r.set_config.payload_variant.lora.region = (meshtastic_Config_LoRaConfig_RegionCode)rngRange(32);
        r.set_config.payload_variant.lora.modem_preset = (meshtastic_Config_LoRaConfig_ModemPreset)rngRange(16);
        r.set_config.payload_variant.lora.use_preset = (rngRange(2) == 0);
        r.set_config.payload_variant.lora.bandwidth = rngRange(512); // includes 0
        r.set_config.payload_variant.lora.channel_num = rngNext();
        break;
    case meshtastic_AdminMessage_set_module_config_tag:
        // Non-beacon variant (beacon derefs global meshBeaconBroadcastModule).
        r.set_module_config.which_payload_variant = SAFE_MODULE_CONFIG_TAGS[rngRange(NUM_SAFE_MODULE_CONFIG_TAGS)];
        break;
    case meshtastic_AdminMessage_set_channel_tag: {
        // Fuzz role + PSK; an empty-PSK SECONDARY at the primary slot used to recurse forever in
        // Channels::getKey.
        meshtastic_Channel &c = r.set_channel;
        c.index = (int32_t)rngRange(16); // includes out-of-range (getByIndex bounds-checks)
        c.has_settings = true;
        c.role = (meshtastic_Channel_Role)rngRange(4);
        fuzzChannelSettings(c.settings);
        break;
    }
    case meshtastic_AdminMessage_add_contact_tag:
        r.add_contact.node_num = fuzzNodeNum();
        r.add_contact.has_user = true;
        r.add_contact.user = fuzzUser();
        r.add_contact.should_ignore = (rngRange(2) == 0);
        r.add_contact.manually_verified = (rngRange(2) == 0);
        break;
    case meshtastic_AdminMessage_remove_fixed_position_tag:
        r.remove_fixed_position = (rngRange(2) == 0);
        break;
    case meshtastic_AdminMessage_set_time_only_tag:
        r.set_time_only = rngNext();
        break;
    default:
        // The remaining safe tags all share a uint32 nodenum union member (set/remove_favorite,
        // set/remove_ignored, toggle_muted, remove_by_nodenum).
        r.set_favorite_node = fuzzNodeNum();
        break;
    }
    return r;
}

void test_E5_admin_dispatch_fuzz(void)
{
    printf("  seed=0x%llx\n", (unsigned long long)(BASE_SEED ^ 0xE5));
    rngSeed(BASE_SEED ^ 0xE5);

    static AdminModuleTestShim admin; // static: MeshModule/OSThread lifetime, see the note in test_E2

    // Seed the local node plus a couple of others for the node-list ops to target.
    mockNodeDB->addNode(LOCAL_NODE);
    mockNodeDB->addNode(REMOTE_NODE);
    mockNodeDB->addNode(0x0C0C0C0C);

    for (unsigned k = 0; k < 6000; k++) {
        admin.deferSaves(); // re-assert each iteration: no disk / reboot regardless of the tag

        meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
        mp.from = 0; // local BLE/USB/TCP client - bypasses the remote auth gates, reaches the switch
        mp.to = LOCAL_NODE;
        mp.id = rngNext();
        mp.channel = (uint8_t)rngRange(8);
        mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        mp.decoded.portnum = meshtastic_PortNum_ADMIN_APP;

        meshtastic_AdminMessage r = fuzzAdminMessage();
        // Contract: never crashes / no OOB regardless of the AdminMessage contents.
        (void)admin.handleReceivedProtobuf(mp, &r);
        admin.drainReply();
    }
    // Reaching here = no crash across all iterations. The node-list ops (set_ignored/add_contact) create
    // nodes, so the DB may fill and evict our seed nodes - that's legitimate; the invariant is only that
    // the count never overran the cap.
    TEST_ASSERT_TRUE_MESSAGE(mockNodeDB->getNumMeshNodes() <= (size_t)MAX_NUM_NODES, "NodeDB overran MAX_NUM_NODES");
}

#if !MESHTASTIC_EXCLUDE_BEACON
// ===========================================================================
// Group E6 - MeshBeaconListenerModule handler over adversarial MeshBeacon
// ===========================================================================
class MeshBeaconListenerModuleTestShim : public MeshBeaconListenerModule
{
  public:
    using MeshBeaconListenerModule::handleReceivedProtobuf;
};

static meshtastic_MeshBeacon fuzzBeacon()
{
    meshtastic_MeshBeacon b = meshtastic_MeshBeacon_init_zero;
    if (rngRange(2)) {
        // Un-terminated: every byte non-NUL, so strnlen(message, sizeof-1) must run to its bound.
        for (size_t i = 0; i < sizeof(b.message); i++)
            b.message[i] = (char)(1 + rngRange(255));
    } else {
        // NUL-terminated random-length prefix.
        size_t n = rngRange(sizeof(b.message));
        for (size_t i = 0; i < sizeof(b.message); i++)
            b.message[i] = (i < n) ? (char)(1 + rngRange(255)) : '\0';
    }
    b.has_offer_channel = (rngRange(2) == 0);
    if (b.has_offer_channel)
        fuzzChannelSettings(b.offer_channel);
    b.offer_region = (meshtastic_Config_LoRaConfig_RegionCode)rngRange(32);
    b.has_offer_preset = (rngRange(2) == 0);
    b.offer_preset = (meshtastic_Config_LoRaConfig_ModemPreset)rngRange(16);
    return b;
}

void test_E6_beacon_listener_fuzz(void)
{
    printf("  seed=0x%llx\n", (unsigned long long)(BASE_SEED ^ 0xE6));
    rngSeed(BASE_SEED ^ 0xE6);

    static MeshBeaconListenerModuleTestShim beacon; // static: MeshModule lifetime, see the note in test_E2

    for (unsigned k = 0; k < 6000; k++) {
        meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
        mp.from = fuzzNodeNum();
        mp.to = NODENUM_BROADCAST;
        mp.id = rngNext();
        mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        mp.decoded.portnum = meshtastic_PortNum_MESH_BEACON_APP;

        meshtastic_MeshBeacon b = fuzzBeacon();
        bool hasOffer =
            b.has_offer_channel || b.offer_region != meshtastic_Config_LoRaConfig_RegionCode_UNSET || b.has_offer_preset;

        // The listener must never consume the packet (it flows on to the phone)...
        TEST_ASSERT_FALSE(beacon.handleReceivedProtobuf(mp, &b));
        // ...and any offer content must land in the client-visible cache, keyed to the sender.
        if (hasOffer) {
            TEST_ASSERT_TRUE(MeshBeaconListenerModule::lastReceivedOffer.valid);
            TEST_ASSERT_EQUAL_UINT32(mp.from, MeshBeaconListenerModule::lastReceivedOffer.sender);
        }
    }
}
#endif // !MESHTASTIC_EXCLUDE_BEACON

// ===========================================================================
// Group E7 - NodeDB::updateUser / updateFrom over adversarial nodeId + payloads
// ===========================================================================
// E2 fuzzes the NodeInfoModule handler; this drives the NodeDB mutators directly with adversarial
// node numbers (0 / self / broadcast / arbitrary) - the path that stores untrusted identity/telemetry.
void test_E7_nodedb_update_fuzz(void)
{
    printf("  seed=0x%llx\n", (unsigned long long)(BASE_SEED ^ 0xE7));
    rngSeed(BASE_SEED ^ 0xE7);

    for (unsigned k = 0; k < 6000; k++) {
        if (rngRange(2)) {
            // updateUser: adversarial User (un-terminated strings, oversized-but-bounded pubkey) under
            // an arbitrary node id and channel index.
            meshtastic_User u = fuzzUser();
            NodeNum id = fuzzNodeNum();
            uint8_t ch = (uint8_t)rngRange(16);
            (void)nodeDB->updateUser(id, u, ch);
        } else {
            // updateFrom: a decoded packet keyed on an arbitrary `from`, exercising the SNR/RSSI/hops
            // ingestion with a random (often un-decodable) inner payload.
            meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
            mp.from = fuzzNodeNum();
            mp.to = (rngRange(2)) ? LOCAL_NODE : NODENUM_BROADCAST;
            mp.id = rngNext();
            mp.rx_snr = (float)((int)rngRange(60) - 30);
            mp.rx_rssi = (int32_t)rngRange(256) - 128;
            mp.hop_start = (uint8_t)rngRange(8);
            mp.hop_limit = (uint8_t)rngRange(8);
            mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
            mp.decoded.portnum = (meshtastic_PortNum)rngRange(80);
            mp.decoded.payload.size = rngRange(sizeof(mp.decoded.payload.bytes) + 1);
            rngFill(mp.decoded.payload.bytes, mp.decoded.payload.size);
            nodeDB->updateFrom(mp);
        }
    }
    // DB stayed internally consistent (count never ran past capacity).
    TEST_ASSERT_TRUE_MESSAGE(mockNodeDB->getNumMeshNodes() <= (size_t)MAX_NUM_NODES, "NodeDB overran MAX_NUM_NODES");
}

// ===========================================================================
// Groups E8-E10 - remaining ProtobufModule handlers driven directly at
// handleReceivedProtobuf. We bypass the ProtobufModule base's allocReply/send
// machinery (calling the handler, not handleReceived), so no router is needed;
// the fixture's nodeDB/service/channels plus the auto-initialized nodeStatus/
// powerStatus globals (defined in main.cpp) are enough. Each module derives from
// OSThread, so each shim is a function-local static (ThreadController lifetime -
// see the note in test_E2). Contract: no crash / no ASan finding on any input.
// ===========================================================================
class PositionModuleTestShim : public PositionModule
{
  public:
    using PositionModule::handleReceivedProtobuf;
};
class DeviceTelemetryModuleTestShim : public DeviceTelemetryModule
{
  public:
    using DeviceTelemetryModule::handleReceivedProtobuf;
};
class NeighborInfoModuleTestShim : public NeighborInfoModule
{
  public:
    using NeighborInfoModule::handleReceivedProtobuf;
};

// Craft an RX MeshPacket header for a decoded-payload handler: adversarial from/to/id/hops, but a
// channel index kept in range (the router resolves and validates mp.channel before dispatch, so an
// out-of-range index can't reach a handler over the air - fuzzing it would test channels.getByIndex,
// not the handler).
static void fuzzRxHeader(meshtastic_MeshPacket &mp, meshtastic_PortNum portnum)
{
    mp.from = fuzzNodeNum();
    mp.to = (rngRange(2)) ? LOCAL_NODE : NODENUM_BROADCAST;
    mp.id = rngNext();
    mp.channel = (uint8_t)rngRange(channels.getNumChannels());
    mp.rx_snr = (float)((int)rngRange(40) - 20);
    mp.rx_rssi = -(int)rngRange(130);
    mp.hop_start = (uint8_t)rngRange(8); // 0..7, wire-bounded
    mp.hop_limit = (uint8_t)rngRange(8);
    mp.want_ack = (rngRange(2) == 0);
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    mp.decoded.portnum = portnum;
}

void test_E8_position_handler_fuzz(void)
{
    printf("  seed=0x%llx\n", (unsigned long long)(BASE_SEED ^ 0xE8));
    rngSeed(BASE_SEED ^ 0xE8);

    static PositionModuleTestShim shim;

    for (unsigned k = 0; k < 6000; k++) {
        meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
        fuzzRxHeader(mp, meshtastic_PortNum_POSITION_APP);
        // Occasionally claim to be from us (drives the fixed_position / setLocalPosition self-update path).
        if (rngRange(4) == 0)
            mp.from = LOCAL_NODE;
        config.position.fixed_position = (rngRange(2) == 0);

        meshtastic_Position p = meshtastic_Position_init_zero;
        p.latitude_i = (int32_t)rngNext();
        p.longitude_i = (int32_t)rngNext();
        p.altitude = (int32_t)rngNext();
        p.altitude_hae = (int32_t)rngNext();
        p.PDOP = rngNext();
        p.sats_in_view = rngNext();
        p.precision_bits = rngRange(40); // includes >32 (the default-precision fallback)
        p.time = (rngRange(2) == 0) ? 0 : rngNext();
        p.timestamp = rngNext();
        (void)shim.handleReceivedProtobuf(mp, &p);
    }
    TEST_ASSERT_TRUE_MESSAGE(mockNodeDB->getNumMeshNodes() <= (size_t)MAX_NUM_NODES, "NodeDB overran MAX_NUM_NODES");
}

void test_E9_telemetry_handler_fuzz(void)
{
    printf("  seed=0x%llx\n", (unsigned long long)(BASE_SEED ^ 0xE9));
    rngSeed(BASE_SEED ^ 0xE9);

    static DeviceTelemetryModuleTestShim shim;

    for (unsigned k = 0; k < 6000; k++) {
        meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
        fuzzRxHeader(mp, meshtastic_PortNum_TELEMETRY_APP);

        meshtastic_Telemetry t = meshtastic_Telemetry_init_zero;
        t.time = rngNext();
        // Cycle across the telemetry variants; only device_metrics is consumed here, the rest must be
        // ignored without touching uninitialized union members.
        switch (rngRange(5)) {
        case 0:
            t.which_variant = meshtastic_Telemetry_device_metrics_tag;
            t.variant.device_metrics.has_battery_level = (rngRange(2) == 0);
            t.variant.device_metrics.battery_level = rngNext();
            t.variant.device_metrics.voltage = (float)((int)rngNext());
            t.variant.device_metrics.channel_utilization = (float)((int)rngNext());
            t.variant.device_metrics.air_util_tx = (float)((int)rngNext());
            t.variant.device_metrics.uptime_seconds = rngNext();
            break;
        case 1:
            t.which_variant = meshtastic_Telemetry_environment_metrics_tag;
            t.variant.environment_metrics.temperature = (float)((int)rngNext());
            t.variant.environment_metrics.relative_humidity = (float)((int)rngNext());
            break;
        case 2:
            t.which_variant = meshtastic_Telemetry_air_quality_metrics_tag;
            t.variant.air_quality_metrics.pm10_standard = rngNext();
            break;
        case 3:
            t.which_variant = meshtastic_Telemetry_power_metrics_tag;
            t.variant.power_metrics.ch1_voltage = (float)((int)rngNext());
            break;
        default:
            t.which_variant = 0; // no variant set
            break;
        }
        (void)shim.handleReceivedProtobuf(mp, &t);
    }
    TEST_ASSERT_TRUE_MESSAGE(mockNodeDB->getNumMeshNodes() <= (size_t)MAX_NUM_NODES, "NodeDB overran MAX_NUM_NODES");
}

void test_E10_neighborinfo_handler_fuzz(void)
{
    printf("  seed=0x%llx\n", (unsigned long long)(BASE_SEED ^ 0xEA));
    rngSeed(BASE_SEED ^ 0xEA);

    static const pb_size_t NB_MAX =
        sizeof(((meshtastic_NeighborInfo *)0)->neighbors) / sizeof(((meshtastic_NeighborInfo *)0)->neighbors[0]); // 10

    static NeighborInfoModuleTestShim shim;

    for (unsigned k = 0; k < 6000; k++) {
        meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
        fuzzRxHeader(mp, meshtastic_PortNum_NEIGHBORINFO_APP);

        meshtastic_NeighborInfo nb = meshtastic_NeighborInfo_init_zero;
        nb.node_id = fuzzNodeNum();
        nb.last_sent_by_id = fuzzNodeNum();
        nb.node_broadcast_interval_secs = rngNext();
        nb.neighbors_count = (pb_size_t)rngRange(NB_MAX + 1); // clamp as nanopb would on decode
        for (pb_size_t i = 0; i < nb.neighbors_count; i++) {
            nb.neighbors[i].node_id = fuzzNodeNum();
            nb.neighbors[i].snr = (float)((int)rngRange(40) - 20);
            nb.neighbors[i].last_rx_time = rngNext();
            nb.neighbors[i].node_broadcast_interval_secs = rngNext();
        }
        (void)shim.handleReceivedProtobuf(mp, &nb);
    }
    TEST_ASSERT_TRUE_MESSAGE(mockNodeDB->getNumMeshNodes() <= (size_t)MAX_NUM_NODES, "NodeDB overran MAX_NUM_NODES");
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();

    printf("\n=== Group E1: perhapsDecode ciphertext fuzz ===\n");
    RUN_TEST(test_E1_perhaps_decode_fuzz);

    printf("\n=== Group E2: NodeInfo handler fuzz ===\n");
    RUN_TEST(test_E2_nodeinfo_handler_fuzz);

    printf("\n=== Group E3: TraceRoute route-processing fuzz ===\n");
    RUN_TEST(test_E3_traceroute_route_processing_fuzz);

    printf("\n=== Group E4: phone-forward gate ===\n");
    RUN_TEST(test_E4_phone_forward_gate);

    printf("\n=== Group E5: Admin dispatch fuzz ===\n");
    RUN_TEST(test_E5_admin_dispatch_fuzz);

#if !MESHTASTIC_EXCLUDE_BEACON
    printf("\n=== Group E6: Beacon listener fuzz ===\n");
    RUN_TEST(test_E6_beacon_listener_fuzz);
#endif

    printf("\n=== Group E7: NodeDB update fuzz ===\n");
    RUN_TEST(test_E7_nodedb_update_fuzz);

    printf("\n=== Group E8: Position handler fuzz ===\n");
    RUN_TEST(test_E8_position_handler_fuzz);

    printf("\n=== Group E9: Telemetry handler fuzz ===\n");
    RUN_TEST(test_E9_telemetry_handler_fuzz);

    printf("\n=== Group E10: NeighborInfo handler fuzz ===\n");
    RUN_TEST(test_E10_neighborinfo_handler_fuzz);

    exit(UNITY_END());
}

void loop() {}

#else // MESHTASTIC_EXCLUDE_PKI

void setUp(void) {}
void tearDown(void) {}
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}
void loop() {}

#endif
