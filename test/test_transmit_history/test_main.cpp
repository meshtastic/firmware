#include "TestUtil.h"
#include "TransmitHistory.h"
#include "gps/RTC.h"
#include <Throttle.h>
#include <unity.h>

// Reset the singleton between tests
static void resetTransmitHistory()
{
    if (transmitHistory) {
        delete transmitHistory;
        transmitHistory = nullptr;
    }
    transmitHistory = TransmitHistory::getInstance();
}

void setUp(void)
{
    resetTransmitHistory();
}

void tearDown(void) {}

static void test_setLastSentToMesh_stores_millis()
{
    transmitHistory->setLastSentToMesh(meshtastic_PortNum_NODEINFO_APP);

    uint32_t result = transmitHistory->getLastSentToMeshMillis(meshtastic_PortNum_NODEINFO_APP);
    TEST_ASSERT_NOT_EQUAL(0, result);

    // The stored millis value should be very close to current millis()
    uint32_t diff = millis() - result;
    TEST_ASSERT_LESS_OR_EQUAL(100, diff); // Within 100ms
}

static void test_set_overwrites_previous_value()
{
    transmitHistory->setLastSentToMesh(meshtastic_PortNum_TELEMETRY_APP);
    uint32_t first = transmitHistory->getLastSentToMeshMillis(meshtastic_PortNum_TELEMETRY_APP);

    testDelay(50);

    transmitHistory->setLastSentToMesh(meshtastic_PortNum_TELEMETRY_APP);
    uint32_t second = transmitHistory->getLastSentToMeshMillis(meshtastic_PortNum_TELEMETRY_APP);

    // The second value should be newer (larger millis)
    TEST_ASSERT_GREATER_THAN(first, second);
}

// --- Throttle integration ---

static void test_throttle_blocks_within_interval()
{
    transmitHistory->setLastSentToMesh(meshtastic_PortNum_NODEINFO_APP);
    uint32_t lastMs = transmitHistory->getLastSentToMeshMillis(meshtastic_PortNum_NODEINFO_APP);

    // Should be within a 10-minute interval (just set it)
    bool withinInterval = Throttle::isWithinTimespanMs(lastMs, 10 * 60 * 1000);
    TEST_ASSERT_TRUE(withinInterval);
}

static void test_throttle_allows_after_interval()
{
    // Unknown key returns 0 — throttle should NOT block
    uint32_t lastMs = transmitHistory->getLastSentToMeshMillis(meshtastic_PortNum_NODEINFO_APP);
    TEST_ASSERT_EQUAL_UINT32(0, lastMs);

    // When lastMs == 0, the module check `lastMs == 0 || !isWithinTimespan` allows sending
    bool shouldSend = (lastMs == 0) || !Throttle::isWithinTimespanMs(lastMs, 10 * 60 * 1000);
    TEST_ASSERT_TRUE(shouldSend);
}

static void test_throttle_blocks_after_set_then_zero_does_not()
{
    // Set it — now throttle should block
    transmitHistory->setLastSentToMesh(meshtastic_PortNum_TELEMETRY_APP);
    uint32_t lastMs = transmitHistory->getLastSentToMeshMillis(meshtastic_PortNum_TELEMETRY_APP);
    bool shouldSend = (lastMs == 0) || !Throttle::isWithinTimespanMs(lastMs, 60 * 60 * 1000);
    TEST_ASSERT_FALSE(shouldSend); // Should be blocked (within 1hr interval)

    // Different key — should allow
    uint32_t otherMs = transmitHistory->getLastSentToMeshMillis(meshtastic_PortNum_POSITION_APP);
    bool otherShouldSend = (otherMs == 0) || !Throttle::isWithinTimespanMs(otherMs, 60 * 60 * 1000);
    TEST_ASSERT_TRUE(otherShouldSend);
}

// --- Multiple keys ---

static void test_multiple_keys_stored_independently()
{
    transmitHistory->setLastSentToMesh(meshtastic_PortNum_NODEINFO_APP);
    uint32_t nodeInfoInitial = transmitHistory->getLastSentToMeshMillis(meshtastic_PortNum_NODEINFO_APP);
    testDelay(20);
    transmitHistory->setLastSentToMesh(meshtastic_PortNum_POSITION_APP);
    uint32_t positionInitial = transmitHistory->getLastSentToMeshMillis(meshtastic_PortNum_POSITION_APP);
    testDelay(20);
    transmitHistory->setLastSentToMesh(meshtastic_PortNum_TELEMETRY_APP);

    uint32_t nodeInfo = transmitHistory->getLastSentToMeshMillis(meshtastic_PortNum_NODEINFO_APP);
    uint32_t position = transmitHistory->getLastSentToMeshMillis(meshtastic_PortNum_POSITION_APP);
    uint32_t telemetry = transmitHistory->getLastSentToMeshMillis(meshtastic_PortNum_TELEMETRY_APP);

    // All should be non-zero
    TEST_ASSERT_NOT_EQUAL(0, nodeInfo);
    TEST_ASSERT_NOT_EQUAL(0, position);
    TEST_ASSERT_NOT_EQUAL(0, telemetry);

    // Updating other keys should not overwrite earlier key timestamps
    TEST_ASSERT_EQUAL_UINT32(nodeInfoInitial, nodeInfo);
    TEST_ASSERT_EQUAL_UINT32(positionInitial, position);
}

// --- Singleton ---

static void test_getInstance_returns_same_instance()
{
    TransmitHistory *a = TransmitHistory::getInstance();
    TransmitHistory *b = TransmitHistory::getInstance();
    TEST_ASSERT_EQUAL_PTR(a, b);
}

static void test_getInstance_creates_global()
{
    if (transmitHistory) {
        delete transmitHistory;
        transmitHistory = nullptr;
    }
    TEST_ASSERT_NULL(transmitHistory);

    TransmitHistory::getInstance();
    TEST_ASSERT_NOT_NULL(transmitHistory);
}

// --- Persistence round-trip (loadFromDisk / saveToDisk) ---

static void test_save_and_load_round_trip()
{
    // Set some values
    transmitHistory->setLastSentToMesh(meshtastic_PortNum_NODEINFO_APP);
    testDelay(10);
    transmitHistory->setLastSentToMesh(meshtastic_PortNum_POSITION_APP);

    uint32_t nodeInfoEpoch = transmitHistory->getLastSentToMeshEpoch(meshtastic_PortNum_NODEINFO_APP);
    uint32_t positionEpoch = transmitHistory->getLastSentToMeshEpoch(meshtastic_PortNum_POSITION_APP);

    // Force save
    transmitHistory->saveToDisk();

    // Reset and reload
    delete transmitHistory;
    transmitHistory = nullptr;
    transmitHistory = TransmitHistory::getInstance();
    transmitHistory->loadFromDisk();

    // Epoch values should be restored (if RTC was available when set)
    uint32_t restoredNodeInfo = transmitHistory->getLastSentToMeshEpoch(meshtastic_PortNum_NODEINFO_APP);
    uint32_t restoredPosition = transmitHistory->getLastSentToMeshEpoch(meshtastic_PortNum_POSITION_APP);

    TEST_ASSERT_EQUAL_UINT32(nodeInfoEpoch, restoredNodeInfo);
    TEST_ASSERT_EQUAL_UINT32(positionEpoch, restoredPosition);

    // After loadFromDisk, millis should be seeded (non-zero) for stored entries
    uint32_t restoredMillis = transmitHistory->getLastSentToMeshMillis(meshtastic_PortNum_NODEINFO_APP);
    if (restoredNodeInfo > 0) {
        // If epoch was stored (set seconds ago), epoch-conversion gives elapsed ≈ 0 s,
        // so getLastSentToMeshMillis() should return a non-zero value.
        TEST_ASSERT_NOT_EQUAL(0, restoredMillis);
    }
}

// --- Boot without RTC scenario ---

// Crash-reboot protection: a send that happened moments before the reboot must still
// throttle after reload. This works because getLastSentToMeshMillis() reconstructs
// a millis()-relative timestamp from the stored epoch, and Throttle uses unsigned
// subtraction so the age survives wraparound even when uptime is near zero.
static void test_boot_after_recent_send_still_throttles()
{
    transmitHistory->setLastSentToMesh(meshtastic_PortNum_NODEINFO_APP);
    transmitHistory->saveToDisk();

    // Simulate reboot
    delete transmitHistory;
    transmitHistory = nullptr;
    transmitHistory = TransmitHistory::getInstance();
    transmitHistory->loadFromDisk();

    // Epoch was set seconds ago; reconstructed age is still within the 10-min window.
    uint32_t result = transmitHistory->getLastSentToMeshMillis(meshtastic_PortNum_NODEINFO_APP);
    uint32_t epoch = transmitHistory->getLastSentToMeshEpoch(meshtastic_PortNum_NODEINFO_APP);
    if (epoch == 0) {
        TEST_IGNORE_MESSAGE("Epoch not persisted; skipping");
        return;
    }

    TEST_ASSERT_NOT_EQUAL(0, result);
    bool withinInterval = Throttle::isWithinTimespanMs(result, 10 * 60 * 1000);
    TEST_ASSERT_TRUE(withinInterval);
}

// Regression test for issue #9901:
// A device powered off for longer than the throttle window must broadcast NodeInfo
// on its next boot — it must not be silenced because loadFromDisk() once treated
// every loaded entry as "just sent" by seeding lastMillis to millis() at boot.
static void test_boot_after_long_gap_allows_nodeinfo()
{
    if (getRTCQuality() <= RTCQualityNone) {
        TEST_IGNORE_MESSAGE("No RTC available; skipping epoch-dependent test");
        return;
    }

    uint32_t now = getTime();

    // Simulate: last NodeInfo sent 30 minutes ago (outside the 10-min throttle window)
    transmitHistory->setLastSentAtEpoch(meshtastic_PortNum_NODEINFO_APP, now - (30 * 60));
    transmitHistory->saveToDisk();

    // Simulate reboot
    delete transmitHistory;
    transmitHistory = nullptr;
    transmitHistory = TransmitHistory::getInstance();
    transmitHistory->loadFromDisk();

    uint32_t restoredEpoch = transmitHistory->getLastSentToMeshEpoch(meshtastic_PortNum_NODEINFO_APP);
    if (restoredEpoch == 0) {
        TEST_IGNORE_MESSAGE("Epoch not persisted; skipping");
        return;
    }

    uint32_t restoredMs = transmitHistory->getLastSentToMeshMillis(meshtastic_PortNum_NODEINFO_APP);
    bool throttled = (restoredMs != 0) && Throttle::isWithinTimespanMs(restoredMs, 10 * 60 * 1000);
    TEST_ASSERT_FALSE_MESSAGE(throttled, "NodeInfo must not be throttled after a 30-min gap (#9901)");
}

// Complementary: a rapid reboot must still throttle (crash-loop protection), even
// though the reconstructed lastMs may wrap because current uptime is small.
static void test_boot_within_throttle_window_still_throttles()
{
    if (getRTCQuality() <= RTCQualityNone) {
        TEST_IGNORE_MESSAGE("No RTC available; skipping epoch-dependent test");
        return;
    }

    uint32_t now = getTime();

    // Simulate: last NodeInfo sent 5 minutes ago (inside the 10-min throttle window)
    transmitHistory->setLastSentAtEpoch(meshtastic_PortNum_NODEINFO_APP, now - (5 * 60));
    transmitHistory->saveToDisk();

    // Simulate reboot
    delete transmitHistory;
    transmitHistory = nullptr;
    transmitHistory = TransmitHistory::getInstance();
    transmitHistory->loadFromDisk();

    uint32_t restoredEpoch = transmitHistory->getLastSentToMeshEpoch(meshtastic_PortNum_NODEINFO_APP);
    if (restoredEpoch == 0) {
        TEST_IGNORE_MESSAGE("Epoch not persisted; skipping");
        return;
    }

    uint32_t restoredMs = transmitHistory->getLastSentToMeshMillis(meshtastic_PortNum_NODEINFO_APP);
    bool throttled = (restoredMs != 0) && Throttle::isWithinTimespanMs(restoredMs, 10 * 60 * 1000);
    TEST_ASSERT_TRUE_MESSAGE(throttled, "NodeInfo must still be throttled when last send was within the 10-min window");
}

static void test_boot_without_time_source_still_throttles_recent_restart()
{
    setBootRelativeTimeForUnitTest(32);
    transmitHistory->setLastSentAtBootRelative(meshtastic_PortNum_NODEINFO_APP, 32);
    transmitHistory->saveToDisk();

    delete transmitHistory;
    transmitHistory = nullptr;
    transmitHistory = TransmitHistory::getInstance();

    setBootRelativeTimeForUnitTest(31);
    transmitHistory->loadFromDisk();

    uint32_t restoredMs = transmitHistory->getLastSentToMeshMillis(meshtastic_PortNum_NODEINFO_APP);
    bool throttled = (restoredMs != 0) && Throttle::isWithinTimespanMs(restoredMs, 10 * 60 * 1000);
    TEST_ASSERT_TRUE_MESSAGE(throttled, "Recent no-RTC reboots should still suppress duplicate NodeInfo");
}

static void test_boot_without_time_source_expires_boot_relative_history()
{
    setBootRelativeTimeForUnitTest(32);
    transmitHistory->setLastSentAtBootRelative(meshtastic_PortNum_NODEINFO_APP, 32);
    transmitHistory->saveToDisk();

    delete transmitHistory;
    transmitHistory = nullptr;
    transmitHistory = TransmitHistory::getInstance();

    setBootRelativeTimeForUnitTest(400);
    transmitHistory->loadFromDisk();

    uint32_t restoredMs = transmitHistory->getLastSentToMeshMillis(meshtastic_PortNum_NODEINFO_APP);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, restoredMs, "Boot-relative history should only suppress near-term restarts");
}

void setup()
{
    initializeTestEnvironment();

    UNITY_BEGIN();

    RUN_TEST(test_setLastSentToMesh_stores_millis);
    RUN_TEST(test_set_overwrites_previous_value);

    RUN_TEST(test_throttle_blocks_within_interval);
    RUN_TEST(test_throttle_allows_after_interval);
    RUN_TEST(test_throttle_blocks_after_set_then_zero_does_not);

    RUN_TEST(test_multiple_keys_stored_independently);

    // Singleton
    RUN_TEST(test_getInstance_returns_same_instance);
    RUN_TEST(test_getInstance_creates_global);

    // Persistence
    RUN_TEST(test_save_and_load_round_trip);
    RUN_TEST(test_boot_after_recent_send_still_throttles);

    // Issue #9901 regression tests
    RUN_TEST(test_boot_after_long_gap_allows_nodeinfo);
    RUN_TEST(test_boot_within_throttle_window_still_throttles);

    // No-RTC regression tests
    RUN_TEST(test_boot_without_time_source_still_throttles_recent_restart);
    RUN_TEST(test_boot_without_time_source_expires_boot_relative_history);

    exit(UNITY_END());
}

void loop() {}
