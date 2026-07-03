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
//
// Handler-path coverage of the remaining ProtobufModule handlers (Position, the Telemetry family,
// NeighborInfo, StoreForward, KeyVerification) is intentionally NOT added here: their constructors
// pull in globals this fixture does not stand up (nodeStatus, GPS, disk-backed state), so driving
// them would test the fixture, not the parser. Those message types are instead fuzzed at the decode
// level in test/test_fuzz_decode (the extended FUZZ_TYPES table), which is where their untrusted-input
// surface actually lives.

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
#include "modules/NodeInfoModule.h"
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

// ---------------------------------------------------------------------------
// Deterministic RNG (seeded LCG)
// ---------------------------------------------------------------------------
static uint64_t g_rng = 0;
static void rngSeed(uint64_t s)
{
    g_rng = s ? s : 0x9E3779B97F4A7C15ULL;
}
static uint32_t rngNext()
{
    g_rng = g_rng * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint32_t)(g_rng >> 32);
}
static uint8_t rngByte()
{
    return (uint8_t)(rngNext() & 0xFF);
}
static uint32_t rngRange(uint32_t n)
{
    return n ? (rngNext() % n) : 0;
}
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

// MockMeshService - the admin/node-op handlers reach `service` for client notifications (the
// protected-node-cap warning path). Release any notification straight back to the pool so 6000
// iterations can't leak it. Mirrors test/test_admin_radio.
class MockMeshService : public MeshService
{
  public:
    void sendClientNotification(meshtastic_ClientNotification *n) override { releaseClientNotificationToPool(n); }
};

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

        size_t n = rngRange(sizeof(p.encrypted.bytes) + 1); // 0..256, always in bounds
        for (size_t i = 0; i < n; i++)
            p.encrypted.bytes[i] = rngByte();
        p.encrypted.size = n;

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
    for (size_t i = 0; i < u.public_key.size && i < sizeof(u.public_key.bytes); i++)
        u.public_key.bytes[i] = rngByte();
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
            size_t n = rngRange(sizeof(mp.decoded.payload.bytes) + 1);
            for (size_t i = 0; i < n; i++)
                mp.decoded.payload.bytes[i] = rngByte();
            mp.decoded.payload.size = n;
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
        for (size_t i = 0; i < n; i++)
            buf[i] = rngByte();
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
// ===========================================================================
// The shim is named AdminModuleTestShim to inherit the friendship AdminModule.h declares for that
// exact name - that is how we reach the protected handler, flip hasOpenEditTransaction, and drain
// myReply. (Separate translation unit from the identically-named shims in test_admin_radio /
// test_mesh_beacon, so no ODR clash.)
class AdminModuleTestShim : public AdminModule
{
  public:
    using AdminModule::handleReceivedProtobuf;
    // Defer persistence: with an "open edit transaction" saveChanges() is a pure no-op - no
    // service->reloadConfig / saveToDisk, and no reboot() - so every setter exercises the in-RAM
    // parse/dispatch/validation logic without disk or reboot side effects.
    void deferSaves() { hasOpenEditTransaction = true; }
    // Setters take the error path (allocErrorResponse) which allocates from packetPool; drain it each
    // iteration or the pool leaks under LSan.
    void drainReply()
    {
        if (myReply) {
            packetPool.release(myReply);
            myReply = nullptr;
        }
    }
};

// A scoped subset of setter / node-list tags - the ones reachable without a fully-booted module graph.
// EXCLUDES: side-effecting tags even with saves deferred (reboot*, shutdown, *factory_reset*,
// nodedb_reset, ota, enter_dfu, delete_file, *_preferences, exit_simulator, begin/commit_edit_settings);
// get_* reply builders (un-mocked service paths); set_owner (needs global nodeInfoModule) and
// set_fixed_position (needs global positionModule) - their payloads are covered by E2/E7 and
// test_fuzz_decode. set_config and set_channel ARE fuzzed - they found two crashes, both now fixed (see
// FINDINGS). The module-config variant is constrained below only to skip the beacon variant's global.
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

// A node number spanning the interesting edges (0, self, broadcast, arbitrary).
static NodeNum fuzzNodeNum()
{
    switch (rngRange(4)) {
    case 0:
        return 0;
    case 1:
        return LOCAL_NODE;
    case 2:
        return NODENUM_BROADCAST;
    default:
        return rngNext();
    }
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
        // manual bandwidth==0 path that used to SIGFPE the validator (FINDINGS #1, fixed).
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
        // Channels::getKey (FINDINGS #2, fixed).
        meshtastic_Channel &c = r.set_channel;
        c.index = (int32_t)rngRange(16); // includes out-of-range (getByIndex bounds-checks)
        c.has_settings = true;
        c.role = (meshtastic_Channel_Role)rngRange(4);
        // Random length, always NUL-terminated (nanopb terminates decoded strings; un-terminated isn't
        // wire-reachable and would only fabricate a strlen OOB).
        size_t nameLen = rngRange(sizeof(c.settings.name));
        for (size_t i = 0; i < sizeof(c.settings.name); i++)
            c.settings.name[i] = (i < nameLen) ? (char)(1 + rngRange(255)) : '\0';
        c.settings.psk.size = rngRange(sizeof(c.settings.psk.bytes) + 1); // 0..32 (0 = empty)
        for (size_t i = 0; i < c.settings.psk.size; i++)
            c.settings.psk.bytes[i] = rngByte();
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
    // NodeDB survived; our seed node is still present and the count never overran the cap.
    TEST_ASSERT_NOT_NULL(mockNodeDB->getMeshNode(REMOTE_NODE));
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
    // message[101]: random prefix, and half the time NO NUL terminator, to stress the
    // strnlen(b->message, sizeof-1) bound in the handler. Prefix avoids embedded NUL so the
    // un-terminated case actually reaches the full buffer.
    size_t n = rngRange(sizeof(b.message) + 4); // may exceed capacity
    for (size_t i = 0; i < sizeof(b.message); i++)
        b.message[i] = (i < n) ? (char)(1 + rngRange(255)) : '\0';
    if (rngRange(2))
        b.message[sizeof(b.message) - 1] = (char)(1 + rngRange(255)); // force un-terminated
    b.has_offer_channel = (rngRange(2) == 0);
    if (b.has_offer_channel) {
        meshtastic_ChannelSettings &s = b.offer_channel;
        size_t nameLen = rngRange(sizeof(s.name)); // NUL-terminated (wire-plausible), see set_channel note
        for (size_t i = 0; i < sizeof(s.name); i++)
            s.name[i] = (i < nameLen) ? (char)(1 + rngRange(255)) : '\0';
        s.psk.size = rngRange(sizeof(s.psk.bytes) + 1);
        for (size_t i = 0; i < s.psk.size; i++)
            s.psk.bytes[i] = rngByte();
    }
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
        // Contract: never over-reads message[] (the strnlen bound) and never crashes on any offer.
        (void)beacon.handleReceivedProtobuf(mp, &b);
    }
    TEST_ASSERT_TRUE(true); // reaching here = no ASan fault across all iterations
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
            size_t n = rngRange(sizeof(mp.decoded.payload.bytes) + 1);
            for (size_t i = 0; i < n; i++)
                mp.decoded.payload.bytes[i] = rngByte();
            mp.decoded.payload.size = n;
            nodeDB->updateFrom(mp);
        }
    }
    // DB stayed internally consistent (count never ran past capacity).
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

// ---------------------------------------------------------------------------
// FINDINGS (surfaced by Group E5 - two remote-DoS crashes reachable from an authorized admin, now FIXED;
// E5 fuzzes both triggers again as regression guards):
//   1. SIGFPE in LoRa validation: set_config with use_preset=false + bandwidth=0 -> numFreqSlots==0 ->
//      hash % numFreqSlots divides by zero. Fixed by guarding the modulo (RadioInterface.cpp).
//   2. Stack overflow in Channels::getKey: a SECONDARY at the primary slot with an empty PSK recursed
//      into getKey(primaryIndex) forever. Fixed by skipping the borrow when chIndex==primaryIndex
//      (Channels.cpp).
// ---------------------------------------------------------------------------
