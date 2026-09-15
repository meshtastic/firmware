// Unit tests for RouterRetirementModule (src/modules/RouterRetirementModule).
//
// Two layers. The pure policy helpers - isRetirableRole / nextRetirementRole / effectiveThresholdSecs /
// shouldRetire - are static and globals-free, and decide *whether* to demote. The stateful layer is
// the hourly credit: accrued in runOnce(), checkpointed to its own file every tick, zeroed and
// re-persisted by noteAdminSession(), and reloaded by a fresh instance (a reboot). Those cases drive
// the real module through RouterRetirementTestShim with the credit file in the suite's sandbox.
#include "Arduino.h"
#include "TestUtil.h"
#include "modules/RouterRetirementModule.h"
#include <unity.h>

#if !MESHTASTIC_EXCLUDE_ROUTER_RETIREMENT

#include "Default.h"
#include "FSCommon.h"
#include "SPILock.h"
#include "main.h" // rebootAtMsec
#include "mesh/Channels.h"
#include "mesh/NodeDB.h"
#include "modules/AdminModule.h"
#include "support/AdminModuleTestShim.h"
#include "support/MockMeshService.h"
#include <cstring>
#include <memory>

using Role = meshtastic_Config_DeviceConfig_Role;
static constexpr Role ROUTER = meshtastic_Config_DeviceConfig_Role_ROUTER;
static constexpr Role ROUTER_LATE = meshtastic_Config_DeviceConfig_Role_ROUTER_LATE;
static constexpr Role CLIENT = meshtastic_Config_DeviceConfig_Role_CLIENT;
static constexpr Role CLIENT_BASE = meshtastic_Config_DeviceConfig_Role_CLIENT_BASE;
static constexpr uint32_t DEF = RouterRetirementModule::DEFAULT_STEP_THRESHOLD_SECS;
static constexpr uint32_t HOUR = RouterRetirementModule::ACCRUE_INTERVAL_SECS;
static constexpr const char *CREDIT_FILE = "/prefs/routerRetirement.bin";

static constexpr NodeNum LOCAL_NODE = 0x0A0A0A0A;
static constexpr NodeNum ADMIN_NODE = 0x0B0B0B0B;    // holds an authorized admin key
static constexpr NodeNum STRANGER_NODE = 0x0D0D0D0D; // no key, no admin channel
static const uint8_t ADMIN_KEY[32] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
                                      0xcc, 0xdd, 0xee, 0xff, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x20};

// The one friend of RouterRetirementModule: exposes the thread body and the credit to the suite.
class RouterRetirementTestShim : public RouterRetirementModule
{
  public:
    using RouterRetirementModule::runOnce;
    uint32_t credit() const { return creditSecs; }
    void setCredit(uint32_t secs) { creditSecs = secs; }
    bool save() const { return saveToDisk(); }
    bool savePending() const { return retirementSavePending; }
};

// The demotion and admin-gate cases need the real NodeDB (role defaults, key lookup, the config
// save) and the real admin dispatch. Built per test, torn down per test.
static NodeDB *testNodeDB = nullptr;
static MockMeshService *mockService = nullptr;
static AdminModuleTestShim *admin = nullptr;

static void buildNodeDbAndAdmin()
{
    mockService = new MockMeshService();
    service = mockService;
    testNodeDB = new NodeDB(); // reloads config/owner from the sandbox, so the fixture is set after
    nodeDB = testNodeDB;
    myNodeInfo.my_node_num = LOCAL_NODE;
    admin = new AdminModuleTestShim();
    admin->deferSaves();

    config = meshtastic_LocalConfig_init_zero;
    moduleConfig = meshtastic_LocalModuleConfig_init_zero;
    config.security.private_key.size = 32;
    memset(config.security.private_key.bytes, 0xA5, 32);
    config.security.admin_key[0].size = 32;
    memcpy(config.security.admin_key[0].bytes, ADMIN_KEY, 32);
    owner = meshtastic_User_init_zero;
    channels.initDefaults();
    channels.onConfigChanged();
}

static void dropNodeDbAndAdmin()
{
    if (admin) {
        admin->drainReply();
        delete admin;
        admin = nullptr;
    }
    service = nullptr;
    delete mockService;
    mockService = nullptr;
    nodeDB = nullptr;
    delete testNodeDB;
    testNodeDB = nullptr;
}

// An admin get_config_request as `from` would deliver it. pki=true carries ADMIN_NODE's key.
static meshtastic_MeshPacket makeGetRequest(NodeNum from, bool pki, meshtastic_AdminMessage &out)
{
    out = meshtastic_AdminMessage_init_zero;
    out.which_payload_variant = meshtastic_AdminMessage_get_config_request_tag;
    out.get_config_request = meshtastic_AdminMessage_ConfigType_DEVICE_CONFIG;

    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = from;
    mp.to = LOCAL_NODE;
    mp.channel = 0;
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    mp.decoded.want_response = true;
    if (pki) {
        mp.pki_encrypted = true;
        mp.public_key.size = 32;
        memcpy(mp.public_key.bytes, ADMIN_KEY, 32);
    }
    return mp;
}

static void removeCreditFile()
{
#ifdef FSCom
    FSCom.remove(CREDIT_FILE);
#endif
}

// A fresh module instance is what a reboot produces: its constructor reloads the credit file.
static std::unique_ptr<RouterRetirementTestShim> boot()
{
    return std::unique_ptr<RouterRetirementTestShim>(new RouterRetirementTestShim());
}

// A retirable role on the default threshold - the accruing configuration.
static void configureAccruing(Role role = ROUTER)
{
    moduleConfig.router_retirement.step_threshold_weeks = 0;
    config.device.role = role;
}

void setUp(void)
{
    config = meshtastic_LocalConfig_init_zero;
    moduleConfig = meshtastic_LocalModuleConfig_init_zero;
    rebootAtMsec = 0;
    removeCreditFile();
}

void tearDown(void)
{
    dropNodeDbAndAdmin();
    removeCreditFile();
}

// --- isRetirableRole ---
void test_isRetirableRole()
{
    TEST_ASSERT_TRUE(RouterRetirementModule::isRetirableRole(ROUTER));
    TEST_ASSERT_TRUE(RouterRetirementModule::isRetirableRole(ROUTER_LATE));
    TEST_ASSERT_FALSE(RouterRetirementModule::isRetirableRole(CLIENT));
    TEST_ASSERT_FALSE(RouterRetirementModule::isRetirableRole(CLIENT_BASE));
}

// --- the slope: ROUTER -> ROUTER_LATE -> CLIENT ---
void test_slope_router_to_router_late()
{
    TEST_ASSERT_EQUAL_INT(ROUTER_LATE, RouterRetirementModule::nextRetirementRole(ROUTER));
}
void test_slope_router_late_to_client()
{
    TEST_ASSERT_EQUAL_INT(CLIENT, RouterRetirementModule::nextRetirementRole(ROUTER_LATE));
}
void test_slope_bottom_is_noop()
{
    // CLIENT is the ladder bottom; non-retirable roles return unchanged (no further demotion).
    TEST_ASSERT_EQUAL_INT(CLIENT, RouterRetirementModule::nextRetirementRole(CLIENT));
    TEST_ASSERT_EQUAL_INT(CLIENT_BASE, RouterRetirementModule::nextRetirementRole(CLIENT_BASE));
}

// --- threshold resolution ---
void test_effectiveThreshold_zero_uses_default()
{
    TEST_ASSERT_EQUAL_UINT32(DEF, RouterRetirementModule::effectiveThresholdSecs(0));
}
void test_effectiveThreshold_converts_weeks_to_seconds()
{
    TEST_ASSERT_EQUAL_UINT32(12u * 7 * 24 * 60 * 60, RouterRetirementModule::effectiveThresholdSecs(12u));
}
void test_effectiveThreshold_saturates_on_overflow()
{
    // 7102 weeks * 604800 s wraps uint32; the threshold must clamp, never come out tiny.
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, RouterRetirementModule::effectiveThresholdSecs(7102u));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, RouterRetirementModule::effectiveThresholdSecs(UINT32_MAX));
    TEST_ASSERT_EQUAL_UINT32(7101u * RouterRetirementModule::WEEK_SECS, RouterRetirementModule::effectiveThresholdSecs(7101u));
}
void test_default_threshold_is_52_weeks()
{
    TEST_ASSERT_EQUAL_UINT32(52u * 7 * 24 * 60 * 60, DEF); // 31,449,600 s
}

// --- shouldRetire ---
void test_shouldRetire_router_at_threshold()
{
    TEST_ASSERT_TRUE(RouterRetirementModule::shouldRetire(ROUTER, DEF, DEF)); // >= boundary
}
void test_shouldRetire_router_short_of_threshold()
{
    TEST_ASSERT_FALSE(RouterRetirementModule::shouldRetire(ROUTER, DEF - 1, DEF));
}
void test_shouldRetire_router_late_at_threshold()
{
    TEST_ASSERT_TRUE(RouterRetirementModule::shouldRetire(ROUTER_LATE, DEF, DEF));
}
void test_shouldRetire_client_never_retires()
{
    // Non-retirable role: even with enormous credit, never demote.
    TEST_ASSERT_FALSE(RouterRetirementModule::shouldRetire(CLIENT, 0xFFFFFFFFu, DEF));
    TEST_ASSERT_FALSE(RouterRetirementModule::shouldRetire(CLIENT_BASE, 0xFFFFFFFFu, DEF));
}

// --- credit accrual and persistence ---

// No file on disk (first boot, or after a factory reset) means no credit.
void test_fresh_boot_starts_at_zero()
{
    auto m = boot();
    TEST_ASSERT_EQUAL_UINT32(0, m->credit());
}

// Each tick adds exactly one interval of seconds - a fixed count, not a millis() difference.
void test_tick_accrues_one_interval()
{
    configureAccruing();
    auto m = boot();
    m->runOnce();
    TEST_ASSERT_EQUAL_UINT32(HOUR, m->credit());
    m->runOnce();
    TEST_ASSERT_EQUAL_UINT32(2 * HOUR, m->credit());
}

// The tick's return value is the accrual cadence in milliseconds - the one place a millisecond appears.
void test_tick_reschedules_one_interval_in_msec()
{
    configureAccruing();
    auto m = boot();
    TEST_ASSERT_EQUAL_INT32(HOUR * 1000, m->runOnce());
}

// Not a router: the tick neither accrues nor writes.
void test_client_role_does_not_accrue()
{
    configureAccruing(CLIENT);
    auto m = boot();
    m->runOnce();
    TEST_ASSERT_EQUAL_UINT32(0, m->credit());
#ifdef FSCom
    TEST_ASSERT_FALSE(FSCom.exists(CREDIT_FILE));
#endif
}

// Always on: a zeroed module config (a fresh flash, or a client that never set it) accrues on the
// 52-week default, nothing has to be switched on first.
void test_zero_config_accrues_on_default()
{
    moduleConfig = meshtastic_LocalModuleConfig_init_zero;
    config.device.role = ROUTER;
    auto m = boot();
    m->runOnce();
    TEST_ASSERT_EQUAL_UINT32(HOUR, m->credit());
}

// The contract the feature rests on: credit is cumulative across reboots. Each tick checkpoints
// the file, and the next boot's constructor reads it back.
void test_credit_survives_reboot()
{
    configureAccruing();
    {
        auto m = boot();
        m->runOnce();
        m->runOnce();
        m->runOnce();
    }
    auto rebooted = boot();
    TEST_ASSERT_EQUAL_UINT32(3 * HOUR, rebooted->credit());
    // ...and keeps counting from there rather than from zero.
    rebooted->runOnce();
    TEST_ASSERT_EQUAL_UINT32(4 * HOUR, rebooted->credit());
}

// An admin session resets the credit, and the reset is on disk at once - a reboot right after a
// successful admin session must not resurrect the old count.
void test_admin_session_resets_and_persists()
{
    configureAccruing();
    {
        auto m = boot();
        m->runOnce();
        m->runOnce();
        TEST_ASSERT_EQUAL_UINT32(2 * HOUR, m->credit());
        m->noteAdminSession();
        TEST_ASSERT_EQUAL_UINT32(0, m->credit());
    }
    auto rebooted = boot();
    TEST_ASSERT_EQUAL_UINT32(0, rebooted->credit());
}

// Admin traffic is chatty (a phone connect is dozens of requests); at zero credit the reset must
// not touch flash at all.
void test_admin_session_at_zero_writes_nothing()
{
    auto m = boot();
    m->noteAdminSession();
#ifdef FSCom
    TEST_ASSERT_FALSE(FSCom.exists(CREDIT_FILE));
#endif
    TEST_ASSERT_EQUAL_UINT32(0, m->credit());
}

// A corrupt or foreign credit file is ignored rather than trusted: starting from zero is the safe
// failure (the node stays a router longer), whereas a garbage count could retire it at once.
void test_corrupt_credit_file_starts_at_zero()
{
#ifdef FSCom
    {
        auto m = boot();
        m->setCredit(5 * HOUR);
        TEST_ASSERT_TRUE(m->save());
    }
    // Overwrite the header so the magic no longer matches.
    auto f = FSCom.open(CREDIT_FILE, FILE_O_WRITE);
    TEST_ASSERT_TRUE(f);
    const uint8_t junk[12] = {0xDE, 0xAD, 0xBE, 0xEF, 1, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF};
    f.write(junk, sizeof(junk));
    f.close();

    auto m = boot();
    TEST_ASSERT_EQUAL_UINT32(0, m->credit());
#else
    TEST_IGNORE_MESSAGE("no filesystem on this target");
#endif
}

// The credit may never wrap: a node left alone for 136 years must retire, not start over.
void test_credit_saturates_instead_of_wrapping()
{
    configureAccruing();
    // Threshold at the counter's ceiling so the tick keeps accruing instead of retiring.
    moduleConfig.router_retirement.step_threshold_weeks = UINT32_MAX; // clamps to UINT32_MAX s
    auto m = boot();
    m->setCredit(UINT32_MAX - HOUR / 2);
    m->runOnce();
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX - HOUR / 2, m->credit());
}

// --- scheduling ---

// OSThread's first run is otherwise immediate, which would bank an hour that never elapsed - and
// a reboot loop could bank one per boot. The first tick must be a full interval away.
void test_first_tick_is_one_full_interval_away()
{
    auto m = boot();
    const long till = m->tillRun(millis());
    TEST_ASSERT_GREATER_THAN_INT32(0, till);
    TEST_ASSERT_LESS_OR_EQUAL_INT32(HOUR * 1000, till);
}

// --- demotion ---

// ROUTER at threshold: one rung down to ROUTER_LATE, ROUTER_LATE's defaults installed, the credit
// file back at zero, and a reboot scheduled. module.proto is removed first so its reappearance
// proves the telemetry interval the defaults changed was actually saved.
void test_router_at_threshold_demotes_to_router_late()
{
    buildNodeDbAndAdmin();
    configureAccruing(ROUTER);
#ifdef FSCom
    FSCom.remove("/prefs/module.proto");
#endif
    {
        auto m = boot();
        m->setCredit(DEF - HOUR);
        m->runOnce();
        TEST_ASSERT_EQUAL_INT(ROUTER_LATE, config.device.role);
        TEST_ASSERT_EQUAL_UINT32(ONE_DAY, moduleConfig.telemetry.device_update_interval);
        TEST_ASSERT_EQUAL_UINT32(0, m->credit());
        TEST_ASSERT_FALSE(m->savePending());
        TEST_ASSERT_NOT_EQUAL(0, rebootAtMsec);
    }
#ifdef FSCom
    TEST_ASSERT_TRUE(FSCom.exists("/prefs/module.proto"));
#endif
    TEST_ASSERT_EQUAL_UINT32(0, boot()->credit());
}

// ROUTER_LATE at threshold: down to CLIENT, and the router-only defaults come off - rebroadcast
// back to ALL, messagable again, telemetry back to the client default - since installRoleDefaults
// has no CLIENT branch to do it.
void test_router_late_at_threshold_demotes_to_client_and_clears_router_defaults()
{
    buildNodeDbAndAdmin();
    configureAccruing(ROUTER_LATE);
    config.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_CORE_PORTNUMS_ONLY;
    moduleConfig.telemetry.device_update_interval = ONE_DAY;
    owner.has_is_unmessagable = true;
    owner.is_unmessagable = true;

    auto m = boot();
    m->setCredit(DEF);
    m->runOnce();

    TEST_ASSERT_EQUAL_INT(CLIENT, config.device.role);
    TEST_ASSERT_EQUAL_INT(meshtastic_Config_DeviceConfig_RebroadcastMode_ALL, config.device.rebroadcast_mode);
    TEST_ASSERT_FALSE(owner.is_unmessagable);
    TEST_ASSERT_EQUAL_UINT32(60 * 60, moduleConfig.telemetry.device_update_interval);
    TEST_ASSERT_NOT_EQUAL(0, rebootAtMsec);
}

// A user-chosen rebroadcast mode is not a router default and is left alone on the way to CLIENT.
void test_client_transition_keeps_a_custom_rebroadcast_mode()
{
    buildNodeDbAndAdmin();
    configureAccruing(ROUTER_LATE);
    config.device.rebroadcast_mode = meshtastic_Config_DeviceConfig_RebroadcastMode_LOCAL_ONLY;

    auto m = boot();
    m->setCredit(DEF);
    m->runOnce();

    TEST_ASSERT_EQUAL_INT(CLIENT, config.device.role);
    TEST_ASSERT_EQUAL_INT(meshtastic_Config_DeviceConfig_RebroadcastMode_LOCAL_ONLY, config.device.rebroadcast_mode);
}

// Below threshold nothing moves: the tick is pure accrual.
void test_below_threshold_keeps_role()
{
    buildNodeDbAndAdmin();
    configureAccruing(ROUTER);
    auto m = boot();
    m->setCredit(DEF - 2 * HOUR);
    m->runOnce();
    TEST_ASSERT_EQUAL_INT(ROUTER, config.device.role);
    TEST_ASSERT_EQUAL_UINT32(0, rebootAtMsec);
}

// --- the admin gate ---

// Only an admin message that passed AdminModule's authorization resets the credit. Anyone able to
// land a decoded ADMIN_APP packet could otherwise keep a router from ever retiring.
void test_unauthorized_remote_admin_does_not_reset()
{
    buildNodeDbAndAdmin();
    routerRetirementModule = new RouterRetirementTestShim();
    auto *m = static_cast<RouterRetirementTestShim *>(routerRetirementModule);
    m->setCredit(5 * HOUR);

    meshtastic_AdminMessage am;
    meshtastic_MeshPacket mp = makeGetRequest(STRANGER_NODE, false, am); // no key, not the admin channel
    admin->handleReceivedProtobuf(mp, &am);
    admin->drainReply();

    TEST_ASSERT_EQUAL_UINT32(5 * HOUR, m->credit());
    delete m;
    routerRetirementModule = nullptr;
}

// A remote setter that fails the session-key gate is rejected before the reset.
void test_remote_setter_without_session_does_not_reset()
{
    buildNodeDbAndAdmin();
    routerRetirementModule = new RouterRetirementTestShim();
    auto *m = static_cast<RouterRetirementTestShim *>(routerRetirementModule);
    m->setCredit(5 * HOUR);

    meshtastic_AdminMessage am = meshtastic_AdminMessage_init_zero;
    am.which_payload_variant = meshtastic_AdminMessage_set_owner_tag;
    strncpy(am.set_owner.long_name, "Hijacked", sizeof(am.set_owner.long_name) - 1);
    meshtastic_AdminMessage unused;
    meshtastic_MeshPacket mp = makeGetRequest(ADMIN_NODE, true, unused); // authorized key, no session
    admin->handleReceivedProtobuf(mp, &am);
    admin->drainReply();

    TEST_ASSERT_EQUAL_UINT32(5 * HOUR, m->credit());
    delete m;
    routerRetirementModule = nullptr;
}

// An authorized remote admin (PKC with an admin key) resets it...
void test_authorized_remote_admin_resets()
{
    buildNodeDbAndAdmin();
    routerRetirementModule = new RouterRetirementTestShim();
    auto *m = static_cast<RouterRetirementTestShim *>(routerRetirementModule);
    m->setCredit(5 * HOUR);

    meshtastic_AdminMessage am;
    meshtastic_MeshPacket mp = makeGetRequest(ADMIN_NODE, true, am);
    admin->handleReceivedProtobuf(mp, &am);
    admin->drainReply();

    TEST_ASSERT_EQUAL_UINT32(0, m->credit());
    delete m;
    routerRetirementModule = nullptr;
}

// ...and so does a local client (USB/BLE, from == 0), which PhoneAPI has already gated.
void test_local_admin_resets()
{
    buildNodeDbAndAdmin();
    routerRetirementModule = new RouterRetirementTestShim();
    auto *m = static_cast<RouterRetirementTestShim *>(routerRetirementModule);
    m->setCredit(5 * HOUR);

    meshtastic_AdminMessage am;
    meshtastic_MeshPacket mp = makeGetRequest(0, false, am);
    admin->handleReceivedProtobuf(mp, &am);
    admin->drainReply();

    TEST_ASSERT_EQUAL_UINT32(0, m->credit());
    delete m;
    routerRetirementModule = nullptr;
}

// A response is a node *we* administer answering us; it says nothing about who manages this node.
void test_admin_response_does_not_reset()
{
    buildNodeDbAndAdmin();
    routerRetirementModule = new RouterRetirementTestShim();
    auto *m = static_cast<RouterRetirementTestShim *>(routerRetirementModule);
    m->setCredit(5 * HOUR);

    meshtastic_AdminMessage am = meshtastic_AdminMessage_init_zero;
    am.which_payload_variant = meshtastic_AdminMessage_get_config_response_tag;
    am.get_config_response.which_payload_variant = meshtastic_Config_device_tag;
    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_zero;
    mp.from = 0; // a local response takes the accepted branch without needing a solicitation
    mp.to = LOCAL_NODE;
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    admin->handleReceivedProtobuf(mp, &am);
    admin->drainReply();

    TEST_ASSERT_EQUAL_UINT32(5 * HOUR, m->credit());
    delete m;
    routerRetirementModule = nullptr;
}

void setup()
{
    initializeTestEnvironment();
    initSPI(); // SafeFile takes spiLock on every write
    UNITY_BEGIN();
    RUN_TEST(test_isRetirableRole);
    RUN_TEST(test_slope_router_to_router_late);
    RUN_TEST(test_slope_router_late_to_client);
    RUN_TEST(test_slope_bottom_is_noop);
    RUN_TEST(test_effectiveThreshold_zero_uses_default);
    RUN_TEST(test_effectiveThreshold_converts_weeks_to_seconds);
    RUN_TEST(test_effectiveThreshold_saturates_on_overflow);
    RUN_TEST(test_default_threshold_is_52_weeks);
    RUN_TEST(test_shouldRetire_router_at_threshold);
    RUN_TEST(test_shouldRetire_router_short_of_threshold);
    RUN_TEST(test_shouldRetire_router_late_at_threshold);
    RUN_TEST(test_shouldRetire_client_never_retires);
    RUN_TEST(test_fresh_boot_starts_at_zero);
    RUN_TEST(test_tick_accrues_one_interval);
    RUN_TEST(test_tick_reschedules_one_interval_in_msec);
    RUN_TEST(test_client_role_does_not_accrue);
    RUN_TEST(test_zero_config_accrues_on_default);
    RUN_TEST(test_credit_survives_reboot);
    RUN_TEST(test_admin_session_resets_and_persists);
    RUN_TEST(test_admin_session_at_zero_writes_nothing);
    RUN_TEST(test_corrupt_credit_file_starts_at_zero);
    RUN_TEST(test_credit_saturates_instead_of_wrapping);
    RUN_TEST(test_first_tick_is_one_full_interval_away);
    RUN_TEST(test_router_at_threshold_demotes_to_router_late);
    RUN_TEST(test_router_late_at_threshold_demotes_to_client_and_clears_router_defaults);
    RUN_TEST(test_client_transition_keeps_a_custom_rebroadcast_mode);
    RUN_TEST(test_below_threshold_keeps_role);
    RUN_TEST(test_unauthorized_remote_admin_does_not_reset);
    RUN_TEST(test_remote_setter_without_session_does_not_reset);
    RUN_TEST(test_authorized_remote_admin_resets);
    RUN_TEST(test_local_admin_resets);
    RUN_TEST(test_admin_response_does_not_reset);
    exit(UNITY_END());
}

void loop() {}

#else // module compiled out - keep the suite linkable/green

void setUp(void) {}
void tearDown(void) {}
void test_excluded_placeholder()
{
    TEST_PASS();
}
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_excluded_placeholder);
    exit(UNITY_END());
}
void loop() {}

#endif
