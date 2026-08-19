// PhoneAPI::getFromRadio() config-dump sequence, asserted on decoded FromRadio protobufs: the
// order client apps depend on, the heartbeat preempt, SPECIAL_NONCE_ONLY_* jumps, mid-dump
// restart, and the post-complete drain reaching idle.
#include "MeshTypes.h"
#include "TestUtil.h"
#include <unity.h>

#include "Channels.h"
#include "CryptoEngine.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PhoneAPI.h"
#include "Router.h"
#include "mesh-pb-constants.h"
#include "meshtastic/admin.pb.h"
#include <cstdio>
#include <cstring>
#include <vector>

// File-scope flag in PhoneAPI.cpp: set by a client heartbeat, cleared by the queueStatus reply.
extern bool heartbeatReceived;

namespace
{
constexpr uint32_t FULL_DUMP_NONCE = 0x51C0FFEE;
constexpr uint32_t SECOND_NONCE = 0x0DDBA11;
constexpr NodeNum SEEDED_NODE_A = 0x00000A01;
constexpr NodeNum SEEDED_NODE_B = 0x00000A02;

constexpr unsigned NUM_SINGLETON_PREFIX = 5; // my_info, deviceuiConfig, own node_info, metadata, region_presets
constexpr unsigned NUM_CONFIG_MESSAGES = _meshtastic_AdminMessage_ConfigType_MAX + 1;
constexpr unsigned NUM_MODULE_CONFIG_MESSAGES = _meshtastic_AdminMessage_ModuleConfigType_MAX + 1;

// STATE_SEND_CONFIG iterates config_state over the AdminMessage ConfigType enum but emits
// Config oneof tags: a proto bump that grows one without the other makes a config message
// carry inner variant 0. The static_asserts turn that drift into a compile error here.
const pb_size_t kExpectedConfigVariants[] = {
    meshtastic_Config_device_tag,    meshtastic_Config_position_tag, meshtastic_Config_power_tag,
    meshtastic_Config_network_tag,   meshtastic_Config_display_tag,  meshtastic_Config_lora_tag,
    meshtastic_Config_bluetooth_tag, meshtastic_Config_security_tag, meshtastic_Config_sessionkey_tag,
    meshtastic_Config_device_ui_tag,
};
static_assert(sizeof(kExpectedConfigVariants) / sizeof(kExpectedConfigVariants[0]) == NUM_CONFIG_MESSAGES,
              "AdminMessage ConfigType enum and Config oneof diverged - update PhoneAPI's STATE_SEND_CONFIG and this list");

const pb_size_t kExpectedModuleConfigVariants[] = {
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
    meshtastic_ModuleConfig_statusmessage_tag,
    meshtastic_ModuleConfig_traffic_management_tag,
    meshtastic_ModuleConfig_tak_tag,
#if !MESHTASTIC_EXCLUDE_BEACON
    meshtastic_ModuleConfig_mesh_beacon_tag,
#else
    0, // beacon compiled out: the slot still ships, as an empty ModuleConfig
#endif
};
static_assert(sizeof(kExpectedModuleConfigVariants) / sizeof(kExpectedModuleConfigVariants[0]) == NUM_MODULE_CONFIG_MESSAGES,
              "AdminMessage ModuleConfigType enum and ModuleConfig oneof diverged - update STATE_SEND_MODULECONFIG and this "
              "list");

/// PhoneAPI over a permanently-connected fake transport.
class PhoneAPITestShim : public PhoneAPI
{
  protected:
    bool checkIsConnected() override { return true; }
};

/// Concrete Router with no radio interface: getQueueStatus() reports an all-zero queue.
class TestRouter : public Router
{
  public:
    // Router's ctor allocated the global cryptLock; nothing else frees it.
    ~TestRouter()
    {
        delete cryptLock;
        cryptLock = nullptr;
    }
};

// Saved-global fixture, template test_event_channel_phone_api. Restored in tearDown() rather
// than by RAII because a failed TEST_ASSERT longjmps out of the test without running destructors.
struct GlobalState {
    MeshService *service;
    Router *router;
    NodeDB *nodeDB;
    concurrency::Lock *cryptLock;
    meshtastic_MyNodeInfo myNodeInfo;
    Channels channels;
    meshtastic_ChannelFile channelFile;
    meshtastic_LocalConfig config;
    meshtastic_LocalModuleConfig moduleConfig;
    meshtastic_DeviceState deviceState;
};

GlobalState *savedState = nullptr;
MeshService *mockService = nullptr;
TestRouter *testRouter = nullptr;
NodeDB *testNodeDB = nullptr;
PhoneAPITestShim *api = nullptr;

/// Give every channel slot a distinct index so the dump's 0..7 ordering is observable.
void configureTestChannels()
{
    channelFile = meshtastic_ChannelFile_init_default;
    channelFile.channels_count = MAX_NUM_CHANNELS;
    for (pb_size_t i = 0; i < MAX_NUM_CHANNELS; i++) {
        channelFile.channels[i].index = (int8_t)i;
        channelFile.channels[i].has_settings = true;
        channelFile.channels[i].role = i == 0 ? meshtastic_Channel_Role_PRIMARY : meshtastic_Channel_Role_SECONDARY;
    }
    channels.onConfigChanged();
}

/// Create a remote node in the scratch NodeDB the way received traffic would.
void seedRemoteNode(NodeNum num)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_zero;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    p.from = num;
    p.to = NODENUM_BROADCAST;
    nodeDB->updateFrom(p);
}

bool sendToRadio(const meshtastic_ToRadio &message)
{
    uint8_t encoded[meshtastic_ToRadio_size] = {};
    const size_t encodedSize =
        pb_encode_to_bytes(encoded, sizeof(encoded), &meshtastic_ToRadio_msg, const_cast<meshtastic_ToRadio *>(&message));
    TEST_ASSERT_GREATER_THAN_UINT(0, encodedSize);
    return api->handleToRadio(encoded, encodedSize);
}

void startHandshake(uint32_t nonce)
{
    meshtastic_ToRadio request = meshtastic_ToRadio_init_zero;
    request.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
    request.want_config_id = nonce;
    sendToRadio(request);
}

void sendPlainHeartbeat()
{
    meshtastic_ToRadio hb = meshtastic_ToRadio_init_zero;
    hb.which_payload_variant = meshtastic_ToRadio_heartbeat_tag;
    hb.heartbeat = meshtastic_Heartbeat_init_zero; // nonce 0 = plain keepalive, expects a queueStatus reply
    sendToRadio(hb);
}

/// One decoded FromRadio pulled off the wire; zero-length reads return false.
bool readOneFromRadio(meshtastic_FromRadio &out)
{
    uint8_t buf[meshtastic_FromRadio_size];
    const size_t len = api->getFromRadio(buf);
    if (len == 0)
        return false;
    out = meshtastic_FromRadio_init_zero;
    TEST_ASSERT_TRUE_MESSAGE(pb_decode_from_bytes(buf, len, &meshtastic_FromRadio_msg, &out),
                             "device emitted an undecodable FromRadio");
    return true;
}

/// Everything the dump emitted, in order, as decoded facts rather than internals.
struct DumpTranscript {
    std::vector<pb_size_t> variants;             // outer which_payload_variant per message
    std::vector<pb_size_t> configVariants;       // inner variant of each FromRadio.config
    std::vector<pb_size_t> moduleConfigVariants; // inner variant of each FromRadio.moduleConfig
    std::vector<int> channelIndices;
    std::vector<uint32_t> nodeNums;
    unsigned fileInfoCount = 0;
    unsigned queueStatusCount = 0;
    uint32_t completeId = 0;
    bool sawComplete = false;
};

/// Pull messages until config_complete_id; false if the stream stalls or overruns the cap.
bool drainUntilComplete(DumpTranscript &t, unsigned maxMessages = 600)
{
    for (unsigned i = 0; i < maxMessages; i++) {
        meshtastic_FromRadio msg;
        if (!readOneFromRadio(msg))
            return false;
        t.variants.push_back(msg.which_payload_variant);
        switch (msg.which_payload_variant) {
        case meshtastic_FromRadio_config_tag:
            t.configVariants.push_back(msg.config.which_payload_variant);
            break;
        case meshtastic_FromRadio_moduleConfig_tag:
            t.moduleConfigVariants.push_back(msg.moduleConfig.which_payload_variant);
            break;
        case meshtastic_FromRadio_channel_tag:
            t.channelIndices.push_back(msg.channel.index);
            break;
        case meshtastic_FromRadio_node_info_tag:
            t.nodeNums.push_back(msg.node_info.num);
            break;
        case meshtastic_FromRadio_fileInfo_tag:
            t.fileInfoCount++;
            break;
        case meshtastic_FromRadio_queueStatus_tag:
            t.queueStatusCount++;
            break;
        case meshtastic_FromRadio_config_complete_id_tag:
            t.completeId = msg.config_complete_id;
            t.sawComplete = true;
            return true;
        default:
            break;
        }
    }
    return false;
}

unsigned countVariant(const DumpTranscript &t, pb_size_t tag)
{
    unsigned n = 0;
    for (pb_size_t v : t.variants)
        if (v == tag)
            n++;
    return n;
}

/// Assert the two non-self node records are the seeded pair (DB iteration order not pinned).
void assertSeededPair(uint32_t first, uint32_t second)
{
    const bool inOrder = first == SEEDED_NODE_A && second == SEEDED_NODE_B;
    const bool swapped = first == SEEDED_NODE_B && second == SEEDED_NODE_A;
    TEST_ASSERT_TRUE_MESSAGE(inOrder || swapped, "other node_infos are not the seeded pair");
}

// --- Tests ---

// The full documented sequence, section by section, ending in the nonce echo. Also pins the
// channel section: exactly MAX_NUM_CHANNELS messages, indices 0..7 in order, between
// region_presets and the first config.
void test_full_want_config_dump_emits_documented_sequence()
{
    seedRemoteNode(SEEDED_NODE_A);
    seedRemoteNode(SEEDED_NODE_B);
    startHandshake(FULL_DUMP_NONCE);

    DumpTranscript t;
    TEST_ASSERT_TRUE_MESSAGE(drainUntilComplete(t), "dump stalled before config_complete_id");

    const pb_size_t expectedPrefix[NUM_SINGLETON_PREFIX] = {
        meshtastic_FromRadio_my_info_tag, meshtastic_FromRadio_deviceuiConfig_tag, meshtastic_FromRadio_node_info_tag,
        meshtastic_FromRadio_metadata_tag, meshtastic_FromRadio_region_presets_tag};
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(NUM_SINGLETON_PREFIX, t.variants.size());
    for (unsigned i = 0; i < NUM_SINGLETON_PREFIX; i++)
        TEST_ASSERT_EQUAL_UINT_MESSAGE(expectedPrefix[i], t.variants[i], "header sequence changed");

    // Bound the raw indexing below: header + channels + configs + moduleConfigs + 2 seeded
    // node_infos + complete is the minimum a full dump can be.
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(
        NUM_SINGLETON_PREFIX + MAX_NUM_CHANNELS + NUM_CONFIG_MESSAGES + NUM_MODULE_CONFIG_MESSAGES + 3, t.variants.size());

    // Channel section: contiguous, complete, ordered.
    const size_t channelStart = NUM_SINGLETON_PREFIX;
    TEST_ASSERT_EQUAL_UINT((unsigned)MAX_NUM_CHANNELS, t.channelIndices.size());
    for (unsigned i = 0; i < MAX_NUM_CHANNELS; i++) {
        TEST_ASSERT_EQUAL_UINT(meshtastic_FromRadio_channel_tag, t.variants[channelStart + i]);
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)i, t.channelIndices[i], "channels must arrive as indices 0..7 in order");
    }

    const size_t configStart = channelStart + MAX_NUM_CHANNELS;
    for (unsigned i = 0; i < NUM_CONFIG_MESSAGES; i++)
        TEST_ASSERT_EQUAL_UINT(meshtastic_FromRadio_config_tag, t.variants[configStart + i]);

    const size_t moduleStart = configStart + NUM_CONFIG_MESSAGES;
    for (unsigned i = 0; i < NUM_MODULE_CONFIG_MESSAGES; i++)
        TEST_ASSERT_EQUAL_UINT(meshtastic_FromRadio_moduleConfig_tag, t.variants[moduleStart + i]);

    // Other node_infos follow the module configs; the own record was already sent in the header.
    const size_t nodesStart = moduleStart + NUM_MODULE_CONFIG_MESSAGES;
    TEST_ASSERT_EQUAL_UINT(3, t.nodeNums.size());
    TEST_ASSERT_EQUAL_UINT32(nodeDB->getNodeNum(), t.nodeNums[0]);
    assertSeededPair(t.nodeNums[1], t.nodeNums[2]);
    TEST_ASSERT_EQUAL_UINT(meshtastic_FromRadio_node_info_tag, t.variants[nodesStart]);
    TEST_ASSERT_EQUAL_UINT(meshtastic_FromRadio_node_info_tag, t.variants[nodesStart + 1]);

    // Everything between the node_infos and the completion id is file manifest (count is
    // whatever the sandbox filesystem holds, so only the position is asserted).
    for (size_t i = nodesStart + 2; i + 1 < t.variants.size(); i++)
        TEST_ASSERT_EQUAL_UINT(meshtastic_FromRadio_fileInfo_tag, t.variants[i]);

    TEST_ASSERT_EQUAL_UINT(meshtastic_FromRadio_config_complete_id_tag, t.variants.back());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(FULL_DUMP_NONCE, t.completeId, "config_complete_id must echo the request nonce");

    // Singletons exactly once, and no stray preempts.
    TEST_ASSERT_EQUAL_UINT(1, countVariant(t, meshtastic_FromRadio_my_info_tag));
    TEST_ASSERT_EQUAL_UINT(1, countVariant(t, meshtastic_FromRadio_deviceuiConfig_tag));
    TEST_ASSERT_EQUAL_UINT(1, countVariant(t, meshtastic_FromRadio_metadata_tag));
    TEST_ASSERT_EQUAL_UINT(1, countVariant(t, meshtastic_FromRadio_region_presets_tag));
    TEST_ASSERT_EQUAL_UINT(1, countVariant(t, meshtastic_FromRadio_config_complete_id_tag));
    TEST_ASSERT_EQUAL_UINT(0, t.queueStatusCount);
    TEST_ASSERT_EQUAL_UINT(NUM_SINGLETON_PREFIX + MAX_NUM_CHANNELS + NUM_CONFIG_MESSAGES + NUM_MODULE_CONFIG_MESSAGES + 2 +
                               t.fileInfoCount + 1,
                           t.variants.size());
}

// Guards the ConfigType-enum-to-oneof-tag iteration: a desync emits a config message whose
// inner variant is 0, which every phone app decodes as an empty Config.
void test_config_section_inner_variants_match_config_type_enum()
{
    startHandshake(FULL_DUMP_NONCE);
    DumpTranscript t;
    TEST_ASSERT_TRUE(drainUntilComplete(t));

    TEST_ASSERT_EQUAL_UINT(NUM_CONFIG_MESSAGES, t.configVariants.size());
    for (unsigned i = 0; i < NUM_CONFIG_MESSAGES; i++) {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0, t.configVariants[i],
                                      "config with inner variant 0: ConfigType enum drifted from the Config oneof");
        TEST_ASSERT_EQUAL_UINT(kExpectedConfigVariants[i], t.configVariants[i]);
    }
}

// Same closed-set guard for the module config section (the drift class already happened once,
// for statusmessage).
void test_module_config_section_inner_variants_match_module_config_type_enum()
{
    startHandshake(FULL_DUMP_NONCE);
    DumpTranscript t;
    TEST_ASSERT_TRUE(drainUntilComplete(t));

    TEST_ASSERT_EQUAL_UINT(NUM_MODULE_CONFIG_MESSAGES, t.moduleConfigVariants.size());
    for (unsigned i = 0; i < NUM_MODULE_CONFIG_MESSAGES; i++) {
        if (kExpectedModuleConfigVariants[i] != 0)
            TEST_ASSERT_NOT_EQUAL_MESSAGE(
                0, t.moduleConfigVariants[i],
                "moduleConfig with inner variant 0: ModuleConfigType enum drifted from the ModuleConfig oneof");
        TEST_ASSERT_EQUAL_UINT(kExpectedModuleConfigVariants[i], t.moduleConfigVariants[i]);
    }
}

// SPECIAL_NONCE_ONLY_NODES jumps straight to the node stream: own record, others, completion -
// no headers, channels, configs, or manifest.
void test_only_nodes_nonce_sends_nodes_then_complete()
{
    seedRemoteNode(SEEDED_NODE_A);
    seedRemoteNode(SEEDED_NODE_B);
    startHandshake(SPECIAL_NONCE_ONLY_NODES);

    DumpTranscript t;
    TEST_ASSERT_TRUE(drainUntilComplete(t));

    TEST_ASSERT_EQUAL_UINT(4, t.variants.size()); // own + 2 seeded + complete
    TEST_ASSERT_EQUAL_UINT(3, t.nodeNums.size());
    TEST_ASSERT_EQUAL_UINT32(nodeDB->getNodeNum(), t.nodeNums[0]);
    assertSeededPair(t.nodeNums[1], t.nodeNums[2]);
    TEST_ASSERT_EQUAL_UINT32(SPECIAL_NONCE_ONLY_NODES, t.completeId);

    TEST_ASSERT_EQUAL_UINT(0, countVariant(t, meshtastic_FromRadio_my_info_tag));
    TEST_ASSERT_EQUAL_UINT(0, countVariant(t, meshtastic_FromRadio_deviceuiConfig_tag));
    TEST_ASSERT_EQUAL_UINT(0, countVariant(t, meshtastic_FromRadio_metadata_tag));
    TEST_ASSERT_EQUAL_UINT(0, countVariant(t, meshtastic_FromRadio_region_presets_tag));
    TEST_ASSERT_EQUAL_UINT(0, countVariant(t, meshtastic_FromRadio_channel_tag));
    TEST_ASSERT_EQUAL_UINT(0, countVariant(t, meshtastic_FromRadio_config_tag));
    TEST_ASSERT_EQUAL_UINT(0, countVariant(t, meshtastic_FromRadio_moduleConfig_tag));
    TEST_ASSERT_EQUAL_UINT(0, t.fileInfoCount);
}

// SPECIAL_NONCE_ONLY_CONFIG delivers the full config but skips the non-self node DB, and must
// not arm the post-complete satellite replay.
void test_only_config_nonce_skips_other_nodeinfos()
{
    seedRemoteNode(SEEDED_NODE_A);
    seedRemoteNode(SEEDED_NODE_B);
    startHandshake(SPECIAL_NONCE_ONLY_CONFIG);

    DumpTranscript t;
    TEST_ASSERT_TRUE(drainUntilComplete(t));

    TEST_ASSERT_EQUAL_UINT(1, countVariant(t, meshtastic_FromRadio_node_info_tag)); // own record only
    TEST_ASSERT_EQUAL_UINT(1, t.nodeNums.size());
    TEST_ASSERT_EQUAL_UINT32(nodeDB->getNodeNum(), t.nodeNums[0]);
    TEST_ASSERT_EQUAL_UINT((unsigned)MAX_NUM_CHANNELS, countVariant(t, meshtastic_FromRadio_channel_tag));
    TEST_ASSERT_EQUAL_UINT(NUM_CONFIG_MESSAGES, t.configVariants.size());
    TEST_ASSERT_EQUAL_UINT(NUM_MODULE_CONFIG_MESSAGES, t.moduleConfigVariants.size());
    TEST_ASSERT_EQUAL_UINT32(SPECIAL_NONCE_ONLY_CONFIG, t.completeId);

    // ONLY_CONFIG skips node/satellite sync entirely: the stream must be idle immediately.
    uint8_t buf[meshtastic_FromRadio_size];
    TEST_ASSERT_EQUAL_UINT(0, api->getFromRadio(buf));
    TEST_ASSERT_FALSE(api->available());
}

// A keepalive heartbeat mid-dump preempts exactly one read with a queueStatus, then the dump
// resumes where it left off; the flag self-clears so nothing repeats or restarts.
void test_heartbeat_mid_dump_preempts_once_then_resumes()
{
    startHandshake(FULL_DUMP_NONCE);

    // Pull the first three header messages, leaving the machine about to send metadata.
    meshtastic_FromRadio msg;
    TEST_ASSERT_TRUE(readOneFromRadio(msg));
    TEST_ASSERT_EQUAL_UINT(meshtastic_FromRadio_my_info_tag, msg.which_payload_variant);
    TEST_ASSERT_TRUE(readOneFromRadio(msg));
    TEST_ASSERT_EQUAL_UINT(meshtastic_FromRadio_deviceuiConfig_tag, msg.which_payload_variant);
    TEST_ASSERT_TRUE(readOneFromRadio(msg));
    TEST_ASSERT_EQUAL_UINT(meshtastic_FromRadio_node_info_tag, msg.which_payload_variant);

    sendPlainHeartbeat();

    TEST_ASSERT_TRUE(readOneFromRadio(msg));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(meshtastic_FromRadio_queueStatus_tag, msg.which_payload_variant,
                                   "heartbeat must be answered with a queueStatus before the dump continues");
    TEST_ASSERT_TRUE(readOneFromRadio(msg));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(meshtastic_FromRadio_metadata_tag, msg.which_payload_variant,
                                   "dump must resume exactly where the heartbeat preempted it");

    DumpTranscript rest;
    TEST_ASSERT_TRUE(drainUntilComplete(rest));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, rest.queueStatusCount, "heartbeat flag must self-clear after one reply");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, countVariant(rest, meshtastic_FromRadio_my_info_tag),
                                   "heartbeat must not restart the dump");
    TEST_ASSERT_EQUAL_UINT32(FULL_DUMP_NONCE, rest.completeId);
}

// Disconnect mid-dump, then a fresh handshake: the machine restarts from my_info with the new
// nonce and every section is delivered exactly once.
void test_close_mid_dump_then_reconnect_restarts_clean()
{
    seedRemoteNode(SEEDED_NODE_A);
    startHandshake(FULL_DUMP_NONCE);

    meshtastic_FromRadio msg;
    for (unsigned i = 0; i < 5; i++)
        TEST_ASSERT_TRUE(readOneFromRadio(msg));

    api->close();
    TEST_ASSERT_FALSE(api->isConnected());
    uint8_t buf[meshtastic_FromRadio_size];
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, api->getFromRadio(buf), "a closed connection must emit nothing");

    startHandshake(SECOND_NONCE);
    DumpTranscript t;
    TEST_ASSERT_TRUE(drainUntilComplete(t));
    TEST_ASSERT_EQUAL_UINT(meshtastic_FromRadio_my_info_tag, t.variants[0]);
    TEST_ASSERT_EQUAL_UINT((unsigned)MAX_NUM_CHANNELS, t.channelIndices.size());
    TEST_ASSERT_EQUAL_UINT(NUM_CONFIG_MESSAGES, t.configVariants.size());
    TEST_ASSERT_EQUAL_UINT(NUM_MODULE_CONFIG_MESSAGES, t.moduleConfigVariants.size());
    TEST_ASSERT_EQUAL_UINT(1, countVariant(t, meshtastic_FromRadio_config_complete_id_tag));
    TEST_ASSERT_EQUAL_UINT32(SECOND_NONCE, t.completeId);
}

// A new want_config while a dump is in flight (no disconnect) also restarts the machine, and
// stale mid-section progress must not leak into the new dump.
void test_rehandshake_mid_dump_restarts_from_my_info()
{
    startHandshake(FULL_DUMP_NONCE);

    // Read into the middle of the config section (5 headers + 8 channels + 7 configs).
    meshtastic_FromRadio msg;
    for (unsigned i = 0; i < NUM_SINGLETON_PREFIX + MAX_NUM_CHANNELS + 7; i++)
        TEST_ASSERT_TRUE(readOneFromRadio(msg));

    startHandshake(SECOND_NONCE);
    DumpTranscript t;
    TEST_ASSERT_TRUE(drainUntilComplete(t));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(meshtastic_FromRadio_my_info_tag, t.variants[0], "re-handshake must restart from my_info");
    TEST_ASSERT_EQUAL_UINT((unsigned)MAX_NUM_CHANNELS, t.channelIndices.size());
    for (unsigned i = 0; i < MAX_NUM_CHANNELS; i++)
        TEST_ASSERT_EQUAL_INT((int)i, t.channelIndices[i]);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(NUM_CONFIG_MESSAGES, t.configVariants.size(),
                                   "stale config_state leaked into the restarted dump");
    TEST_ASSERT_EQUAL_UINT(NUM_MODULE_CONFIG_MESSAGES, t.moduleConfigVariants.size());
    TEST_ASSERT_EQUAL_UINT32(SECOND_NONCE, t.completeId);
}

// After config_complete_id the trailing satellite replay must reach idle in bounded reads - a
// drain loop keyed on available() must terminate (the infinite-drain regression class).
void test_dump_reaches_idle_after_complete()
{
    seedRemoteNode(SEEDED_NODE_A);
    seedRemoteNode(SEEDED_NODE_B);
    startHandshake(FULL_DUMP_NONCE);

    DumpTranscript t;
    TEST_ASSERT_TRUE(drainUntilComplete(t));

    uint8_t buf[meshtastic_FromRadio_size];
    bool idle = false;
    for (unsigned i = 0; i < 8 && !idle; i++) {
        if (!api->available())
            idle = true;
        else
            api->getFromRadio(buf); // replay drain: empty phases must advance toward idle
    }
    TEST_ASSERT_TRUE_MESSAGE(idle, "post-complete drain never went idle: available() stuck true");
    TEST_ASSERT_EQUAL_UINT(0, api->getFromRadio(buf));
}

} // namespace

void setUp(void)
{
    savedState =
        new GlobalState{service, router, nodeDB, cryptLock, myNodeInfo, channels, channelFile, config, moduleConfig, devicestate};

    service = mockService = new MeshService();
    // A real boot starts with a zeroed nodeDatabase; in-process the global retains the previous
    // test's vector (the decode callback appends, it does not clear), so reset it first.
    nodeDatabase.version = 0;
    nodeDatabase.nodes.clear();
    nodeDB = testNodeDB = new NodeDB();
    configureTestChannels();
    cryptLock = nullptr; // Router's ctor asserts this is unset before allocating its own.
    router = testRouter = new TestRouter();
    api = new PhoneAPITestShim();
    heartbeatReceived = false;
}

void tearDown(void)
{
    delete api; // dtor runs close(), which still needs the mock service installed
    api = nullptr;
    delete testRouter; // ~TestRouter() deletes the cryptLock its ctor allocated
    testRouter = nullptr;
    delete testNodeDB;
    testNodeDB = nullptr;
    delete mockService;
    mockService = nullptr;
    heartbeatReceived = false;

    service = savedState->service;
    router = savedState->router;
    nodeDB = savedState->nodeDB;
    cryptLock = savedState->cryptLock; // ~TestRouter() nulled it; hand the saved router its own back
    myNodeInfo = savedState->myNodeInfo;
    channels = savedState->channels;
    channelFile = savedState->channelFile;
    config = savedState->config;
    moduleConfig = savedState->moduleConfig;
    devicestate = savedState->deviceState;
    delete savedState;
    savedState = nullptr;
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();

    printf("\n=== want_config dump sequence ===\n");
    RUN_TEST(test_full_want_config_dump_emits_documented_sequence);
    RUN_TEST(test_config_section_inner_variants_match_config_type_enum);
    RUN_TEST(test_module_config_section_inner_variants_match_module_config_type_enum);

    printf("\n=== special nonces ===\n");
    RUN_TEST(test_only_nodes_nonce_sends_nodes_then_complete);
    RUN_TEST(test_only_config_nonce_skips_other_nodeinfos);

    printf("\n=== preemption and restart ===\n");
    RUN_TEST(test_heartbeat_mid_dump_preempts_once_then_resumes);
    RUN_TEST(test_close_mid_dump_then_reconnect_restarts_clean);
    RUN_TEST(test_rehandshake_mid_dump_restarts_from_my_info);
    RUN_TEST(test_dump_reaches_idle_after_complete);

    exit(UNITY_END());
}

void loop() {}
