// TAK (ATAK) team/role value fidelity: the fields set by the app must come back verbatim.
//
// Companion to test_module_config, which proves every submessage's presence survives the
// set -> save -> load -> get lifecycle but sweeps with zero-valued payloads and so cannot tell a
// copied field from an ignored one. These tests pin the values themselves for the submessage that
// shipped broken (firmware#11182 / Meshtastic-Android#6430: team/role reverted to "unspecified"
// after every save).

#include "MeshTypes.h" // include BEFORE TestUtil.h
#include "TestUtil.h"
#include <unity.h>

#include "mesh/NodeDB.h"
#include "mesh/mesh-pb-constants.h"
#include "modules/AdminModule.h"
#include "support/AdminModuleTestShim.h"
#include "support/MockMeshService.h"
#include <cstring>

static constexpr NodeNum LOCAL_NODE = 0x0A0A0A0A;
static constexpr NodeNum ADMIN_NODE = 0x0B0B0B0B;

static MockMeshService *mockService = nullptr;
static AdminModuleTestShim *admin = nullptr;

// A set_module_config payload carrying the tak submessage, as the phone app sends it.
static meshtastic_ModuleConfig makeTakModuleConfig(meshtastic_Team team, meshtastic_MemberRole role)
{
    meshtastic_ModuleConfig mc = meshtastic_ModuleConfig_init_zero;
    mc.which_payload_variant = meshtastic_ModuleConfig_tak_tag;
    mc.payload_variant.tak.team = team;
    mc.payload_variant.tak.role = role;
    return mc;
}

static meshtastic_MeshPacket makeGetRequest(NodeNum from)
{
    meshtastic_MeshPacket req = meshtastic_MeshPacket_init_zero;
    req.from = from;
    req.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    req.decoded.want_response = true;
    return req;
}

// Pull the TAKConfig out of the get_module_config response a handler queued in myReply.
static bool decodeTakFromReply(meshtastic_MeshPacket *reply, meshtastic_ModuleConfig_TAKConfig &out)
{
    meshtastic_AdminMessage am = meshtastic_AdminMessage_init_zero;
    if (!reply || reply->which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        return false;
    if (!pb_decode_from_bytes(reply->decoded.payload.bytes, reply->decoded.payload.size, &meshtastic_AdminMessage_msg, &am))
        return false;
    if (am.which_payload_variant != meshtastic_AdminMessage_get_module_config_response_tag ||
        am.get_module_config_response.which_payload_variant != meshtastic_ModuleConfig_tak_tag)
        return false;
    out = am.get_module_config_response.payload_variant.tak;
    return true;
}

void test_setStoresTeamAndRoleVerbatim(void)
{
    TEST_ASSERT_TRUE(admin->handleSetModuleConfig(makeTakModuleConfig(meshtastic_Team_Cyan, meshtastic_MemberRole_Medic)));

    TEST_ASSERT_TRUE(moduleConfig.has_tak);
    TEST_ASSERT_EQUAL(meshtastic_Team_Cyan, moduleConfig.tak.team);
    TEST_ASSERT_EQUAL(meshtastic_MemberRole_Medic, moduleConfig.tak.role);
}

// Clearing back to the zero values is a real edit, not an absent field: has_tak must stay set so the
// cleared state is what gets written, rather than the previous value surviving.
void test_clearingToUnspecifiedIsStored(void)
{
    admin->handleSetModuleConfig(makeTakModuleConfig(meshtastic_Team_Red, meshtastic_MemberRole_TeamLead));
    admin->handleSetModuleConfig(makeTakModuleConfig(meshtastic_Team_Unspecifed_Color, meshtastic_MemberRole_Unspecifed));

    TEST_ASSERT_TRUE(moduleConfig.has_tak);
    TEST_ASSERT_EQUAL(meshtastic_Team_Unspecifed_Color, moduleConfig.tak.team);
    TEST_ASSERT_EQUAL(meshtastic_MemberRole_Unspecifed, moduleConfig.tak.role);
}

// The reported symptom end to end: values set, saved, reloaded (as a reboot would), and read back.
void test_valuesSurviveSaveLoad(void)
{
    admin->handleSetModuleConfig(makeTakModuleConfig(meshtastic_Team_Magenta, meshtastic_MemberRole_Sniper));

    uint8_t buf[meshtastic_LocalModuleConfig_size];
    size_t len = pb_encode_to_bytes(buf, sizeof(buf), &meshtastic_LocalModuleConfig_msg, &moduleConfig);

    moduleConfig = meshtastic_LocalModuleConfig_init_zero;
    TEST_ASSERT_TRUE(pb_decode_from_bytes(buf, len, &meshtastic_LocalModuleConfig_msg, &moduleConfig));

    TEST_ASSERT_TRUE(moduleConfig.has_tak);
    TEST_ASSERT_EQUAL(meshtastic_Team_Magenta, moduleConfig.tak.team);
    TEST_ASSERT_EQUAL(meshtastic_MemberRole_Sniper, moduleConfig.tak.role);
}

// Remote and local admin reads take the same branch; both must carry the stored values unredacted.
void test_getReturnsStoredValues(void)
{
    admin->handleSetModuleConfig(makeTakModuleConfig(meshtastic_Team_Dark_Blue, meshtastic_MemberRole_K9));

    const NodeNum requesters[] = {ADMIN_NODE, 0};
    for (NodeNum from : requesters) {
        meshtastic_MeshPacket req = makeGetRequest(from);
        admin->handleGetModuleConfig(req, meshtastic_AdminMessage_ModuleConfigType_TAK_CONFIG);

        meshtastic_ModuleConfig_TAKConfig tak;
        TEST_ASSERT_TRUE_MESSAGE(decodeTakFromReply(admin->reply(), tak), "TAK_CONFIG request must produce a tak response");
        TEST_ASSERT_EQUAL(meshtastic_Team_Dark_Blue, tak.team);
        TEST_ASSERT_EQUAL(meshtastic_MemberRole_K9, tak.role);
        admin->drainReply();
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

    RUN_TEST(test_setStoresTeamAndRoleVerbatim);
    RUN_TEST(test_clearingToUnspecifiedIsStored);
    RUN_TEST(test_valuesSurviveSaveLoad);
    RUN_TEST(test_getReturnsStoredValues);

    exit(UNITY_END());
}

void loop() {}
