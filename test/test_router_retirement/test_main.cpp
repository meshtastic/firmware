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

#include "FSCommon.h"
#include "SPILock.h"
#include "mesh/NodeDB.h"
#include <memory>

using Role = meshtastic_Config_DeviceConfig_Role;
static constexpr Role ROUTER = meshtastic_Config_DeviceConfig_Role_ROUTER;
static constexpr Role ROUTER_LATE = meshtastic_Config_DeviceConfig_Role_ROUTER_LATE;
static constexpr Role CLIENT = meshtastic_Config_DeviceConfig_Role_CLIENT;
static constexpr Role CLIENT_BASE = meshtastic_Config_DeviceConfig_Role_CLIENT_BASE;
static constexpr uint32_t DEF = RouterRetirementModule::DEFAULT_STEP_THRESHOLD_SECS;
static constexpr uint32_t HOUR = RouterRetirementModule::ACCRUE_INTERVAL_SECS;
static constexpr const char *CREDIT_FILE = "/prefs/routerRetirement.bin";

// The one friend of RouterRetirementModule: exposes the thread body and the credit to the suite.
class RouterRetirementTestShim : public RouterRetirementModule
{
  public:
    using RouterRetirementModule::runOnce;
    uint32_t credit() const { return creditSecs; }
    void setCredit(uint32_t secs) { creditSecs = secs; }
    bool save() const { return saveToDisk(); }
};

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

// Enabled, retirable role, default threshold - the accruing configuration.
static void configureAccruing(Role role = ROUTER)
{
    moduleConfig.router_retirement.enabled = true;
    moduleConfig.router_retirement.step_threshold_secs = 0;
    config.device.role = role;
}

void setUp(void)
{
    config = meshtastic_LocalConfig_init_zero;
    moduleConfig = meshtastic_LocalModuleConfig_init_zero;
    removeCreditFile();
}

void tearDown(void)
{
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
void test_effectiveThreshold_nonzero_used_verbatim()
{
    TEST_ASSERT_EQUAL_UINT32(1234u, RouterRetirementModule::effectiveThresholdSecs(1234u));
}
void test_default_threshold_is_90_days()
{
    TEST_ASSERT_EQUAL_UINT32(90u * 24 * 60 * 60, DEF); // 7,776,000 s
}

// --- shouldRetire ---
void test_shouldRetire_disabled_never_retires()
{
    TEST_ASSERT_FALSE(RouterRetirementModule::shouldRetire(false, ROUTER, DEF + 1, DEF));
}
void test_shouldRetire_router_at_threshold()
{
    TEST_ASSERT_TRUE(RouterRetirementModule::shouldRetire(true, ROUTER, DEF, DEF)); // >= boundary
}
void test_shouldRetire_router_below_threshold()
{
    TEST_ASSERT_FALSE(RouterRetirementModule::shouldRetire(true, ROUTER, DEF - 1, DEF));
}
void test_shouldRetire_router_late_at_threshold()
{
    TEST_ASSERT_TRUE(RouterRetirementModule::shouldRetire(true, ROUTER_LATE, DEF, DEF));
}
void test_shouldRetire_client_never_retires()
{
    // Non-retirable role: even with enormous credit, never demote.
    TEST_ASSERT_FALSE(RouterRetirementModule::shouldRetire(true, CLIENT, 0xFFFFFFFFu, DEF));
    TEST_ASSERT_FALSE(RouterRetirementModule::shouldRetire(true, CLIENT_BASE, 0xFFFFFFFFu, DEF));
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
void test_tick_reschedules_one_interval_in_ms()
{
    configureAccruing();
    auto m = boot();
    TEST_ASSERT_EQUAL_INT32(HOUR * 1000, m->runOnce());
}

// Disabled, or not a router: the tick neither accrues nor writes.
void test_disabled_does_not_accrue()
{
    configureAccruing();
    moduleConfig.router_retirement.enabled = false;
    auto m = boot();
    m->runOnce();
    TEST_ASSERT_EQUAL_UINT32(0, m->credit());
#ifdef FSCom
    TEST_ASSERT_FALSE(FSCom.exists(CREDIT_FILE));
#endif
}
void test_client_role_does_not_accrue()
{
    configureAccruing(CLIENT);
    auto m = boot();
    m->runOnce();
    TEST_ASSERT_EQUAL_UINT32(0, m->credit());
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
    // Threshold far above the counter's ceiling so the tick keeps accruing instead of retiring.
    moduleConfig.router_retirement.step_threshold_secs = UINT32_MAX;
    auto m = boot();
    m->setCredit(UINT32_MAX - HOUR / 2);
    m->runOnce();
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX - HOUR / 2, m->credit());
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
    RUN_TEST(test_effectiveThreshold_nonzero_used_verbatim);
    RUN_TEST(test_default_threshold_is_90_days);
    RUN_TEST(test_shouldRetire_disabled_never_retires);
    RUN_TEST(test_shouldRetire_router_at_threshold);
    RUN_TEST(test_shouldRetire_router_below_threshold);
    RUN_TEST(test_shouldRetire_router_late_at_threshold);
    RUN_TEST(test_shouldRetire_client_never_retires);
    RUN_TEST(test_fresh_boot_starts_at_zero);
    RUN_TEST(test_tick_accrues_one_interval);
    RUN_TEST(test_tick_reschedules_one_interval_in_ms);
    RUN_TEST(test_disabled_does_not_accrue);
    RUN_TEST(test_client_role_does_not_accrue);
    RUN_TEST(test_credit_survives_reboot);
    RUN_TEST(test_admin_session_resets_and_persists);
    RUN_TEST(test_admin_session_at_zero_writes_nothing);
    RUN_TEST(test_corrupt_credit_file_starts_at_zero);
    RUN_TEST(test_credit_saturates_instead_of_wrapping);
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
