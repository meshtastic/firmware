// AdminModule module-config lifecycle coverage: every submessage must survive set -> save -> load -> get.
//
// Motivated by firmware#11182 / Meshtastic-Android#6430 (TAK team/role never persisted): a submessage
// with no case in handleSetModuleConfig() falls through the switch silently - the setter still ACKs
// and reboots - and one missing has_* flag in saveToDisk() makes nanopb drop the struct on the flash
// write. Both defects are invisible to per-module tests until someone hits them in the field, so the
// sweeps below enumerate the generated constants and put each type through the full lifecycle.
//
// The sweeps assert presence and routing, not field semantics - per-module value validation belongs
// to the per-module suites (test_mesh_beacon, test_admin_session_repro, ...).

#include "MeshTypes.h" // include BEFORE TestUtil.h
#include "TestUtil.h"
#include <unity.h>

#include "mesh/NodeDB.h"
#include "mesh/mesh-pb-constants.h"
#include "modules/AdminModule.h"
#include "support/AdminModuleTestShim.h"
#include "support/MockMeshService.h"
#include <cstdio>
#include <cstring>

static constexpr NodeNum LOCAL_NODE = 0x0A0A0A0A;
static constexpr NodeNum ADMIN_NODE = 0x0B0B0B0B;

static MockMeshService *mockService = nullptr;
static AdminModuleTestShim *admin = nullptr;

// Both generated sets are contiguous: ModuleConfigType runs 0.._MAX and the matching ModuleConfig
// submessage tags run 1.._MAX+1, so type + 1 == tag and no lookup table is needed.
static const uint32_t moduleConfigTypeCount = _meshtastic_AdminMessage_ModuleConfigType_MAX + 1;

// The submessages whose handlers are compiled out on some builds. Everything else must be present
// unconditionally.
static bool moduleConfigIsCompiledOut(pb_size_t tag)
{
    (void)tag;
#if MESHTASTIC_EXCLUDE_MQTT
    if (tag == meshtastic_ModuleConfig_mqtt_tag)
        return true;
#endif
#if MESHTASTIC_EXCLUDE_BEACON
    if (tag == meshtastic_ModuleConfig_mesh_beacon_tag)
        return true;
#endif
    return false;
}

// Unity assertion messages are plain strings, so name the failing iteration by number - nanopb keeps
// no field names to print. Cross-reference module_config.pb.h / admin.pb.h.
static const char *describeTag(const char *what, pb_size_t tag)
{
    static char buf[96];
    snprintf(buf, sizeof(buf), "%s: ModuleConfig submessage tag %u", what, (unsigned)tag);
    return buf;
}

static meshtastic_MeshPacket makeGetRequest(NodeNum from)
{
    meshtastic_MeshPacket req = meshtastic_MeshPacket_init_zero;
    req.from = from;
    req.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    req.decoded.want_response = true;
    return req;
}

// Read back the variant tag of whatever get_module_config response the handler queued.
static bool decodeResponseTagFromReply(meshtastic_MeshPacket *reply, pb_size_t &out)
{
    meshtastic_AdminMessage am = meshtastic_AdminMessage_init_zero;
    if (!reply || reply->which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        return false;
    if (!pb_decode_from_bytes(reply->decoded.payload.bytes, reply->decoded.payload.size, &meshtastic_AdminMessage_msg, &am))
        return false;
    if (am.which_payload_variant != meshtastic_AdminMessage_get_module_config_response_tag)
        return false;
    out = am.get_module_config_response.which_payload_variant;
    return true;
}

// The proto guarantees the two enumerations stay in step; the sweeps below rely on it to pair a
// ModuleConfigType with its submessage tag without a lookup table.
void test_tagRangeMatchesTypeRange(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(moduleConfigTypeCount, meshtastic_ModuleConfig_mesh_beacon_tag,
                              "ModuleConfigType and ModuleConfig submessage tags are no longer 1:1");
}

// The full lifecycle per submessage: admin set -> encode (what saveToDisk writes) -> decode into the
// live global (a reboot's loadFromDisk) -> admin get answers with that submessage. A missing set case
// shows up as a 0-byte encode; a missing get case as an empty or mismatched response.
void test_everyModuleConfig_survivesSetSaveLoadGet(void)
{
    for (pb_size_t tag = 1; tag <= moduleConfigTypeCount; tag++) {
        if (moduleConfigIsCompiledOut(tag))
            continue;
        moduleConfig = meshtastic_LocalModuleConfig_init_zero;

        // Set: the only content is this one submessage, so the encode below stands or falls with it.
        meshtastic_ModuleConfig mc = meshtastic_ModuleConfig_init_zero;
        mc.which_payload_variant = tag;
        TEST_ASSERT_TRUE_MESSAGE(admin->handleSetModuleConfig(mc), describeTag("setter rejected", tag));

        // Save: presence flag unset means nanopb drops the struct and the write encodes to nothing.
        uint8_t saved[meshtastic_LocalModuleConfig_size];
        size_t savedLen = pb_encode_to_bytes(saved, sizeof(saved), &meshtastic_LocalModuleConfig_msg, &moduleConfig);
        TEST_ASSERT_GREATER_THAN_size_t_MESSAGE(0, savedLen, describeTag("dropped on save, presence flag not set", tag));

        // Load: overwrite the live global, as a reboot would.
        moduleConfig = meshtastic_LocalModuleConfig_init_zero;
        TEST_ASSERT_TRUE_MESSAGE(pb_decode_from_bytes(saved, savedLen, &meshtastic_LocalModuleConfig_msg, &moduleConfig),
                                 describeTag("failed to decode", tag));

        // Re-encoding what was loaded must reproduce the saved bytes - presence survived the reload.
        uint8_t reloaded[meshtastic_LocalModuleConfig_size];
        size_t reloadedLen = pb_encode_to_bytes(reloaded, sizeof(reloaded), &meshtastic_LocalModuleConfig_msg, &moduleConfig);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(savedLen, reloadedLen, describeTag("changed size across reload", tag));
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(saved, reloaded, savedLen, describeTag("changed content across reload", tag));

        // Get: remote and local requests take the same branch and must answer with this submessage.
        const NodeNum requesters[] = {ADMIN_NODE, 0};
        for (NodeNum from : requesters) {
            meshtastic_MeshPacket req = makeGetRequest(from);
            admin->handleGetModuleConfig(req, tag - 1); // type pairs 1:1 with tag (guard test above)

            pb_size_t responseTag = 0;
            bool decoded = decodeResponseTagFromReply(admin->reply(), responseTag);
            admin->drainReply();

            TEST_ASSERT_TRUE_MESSAGE(decoded, describeTag("no get response", tag));
            TEST_ASSERT_EQUAL_MESSAGE(tag, responseTag, describeTag("get answered with a different submessage", tag));
        }
    }
}

// Each set is a single-submessage edit: writing type N must not erase types 1..N-1. Every additional
// stored submessage grows the encoded config, so a clobbered earlier write shows up as a non-increase.
void test_sequentialSets_preserveEarlierWrites(void)
{
    size_t prevLen = 0;
    for (pb_size_t tag = 1; tag <= moduleConfigTypeCount; tag++) {
        if (moduleConfigIsCompiledOut(tag))
            continue;

        meshtastic_ModuleConfig mc = meshtastic_ModuleConfig_init_zero;
        mc.which_payload_variant = tag;
        admin->handleSetModuleConfig(mc);

        uint8_t buf[meshtastic_LocalModuleConfig_size];
        size_t len = pb_encode_to_bytes(buf, sizeof(buf), &meshtastic_LocalModuleConfig_msg, &moduleConfig);
        TEST_ASSERT_GREATER_THAN_size_t_MESSAGE(prevLen, len, describeTag("set erased an earlier submessage", tag));
        prevLen = len;
    }
}

void setUp(void)
{
    mockService = new MockMeshService();
    service = mockService;

    admin = new AdminModuleTestShim();
    admin->deferSaves(); // no disk write or reboot when a setter is accepted

    moduleConfig = meshtastic_LocalModuleConfig_init_zero;
    config = meshtastic_LocalConfig_init_zero;
    myNodeInfo = meshtastic_MyNodeInfo_init_zero;
    myNodeInfo.my_node_num = LOCAL_NODE;
}

void tearDown(void)
{
    admin->drainReply();
    delete admin;
    admin = nullptr;

    service = nullptr;
    delete mockService;
    mockService = nullptr;
}

void setup()
{
    delay(10);
    initializeTestEnvironment();
    UNITY_BEGIN();

    RUN_TEST(test_tagRangeMatchesTypeRange);
    RUN_TEST(test_everyModuleConfig_survivesSetSaveLoadGet);
    RUN_TEST(test_sequentialSets_preserveEarlierWrites);

    exit(UNITY_END());
}

void loop() {}
