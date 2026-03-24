#include "TestUtil.h"
#include "TransmitHistory.h"
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
        // If epoch was stored, millis should be seeded from load
        TEST_ASSERT_NOT_EQUAL(0, restoredMillis);
    }
}

// --- Boot without RTC scenario ---

static void test_load_seeds_millis_even_without_rtc()
{
    // This tests the critical crash-reboot scenario:
    // After loadFromDisk(), even if getTime() returns 0 (no RTC),
    // lastMillis should be seeded so throttle blocks immediate re-broadcast.

    transmitHistory->setLastSentToMesh(meshtastic_PortNum_NODEINFO_APP);
    transmitHistory->saveToDisk();

    // Simulate reboot: destroy and recreate
    delete transmitHistory;
    transmitHistory = nullptr;
    transmitHistory = TransmitHistory::getInstance();
    transmitHistory->loadFromDisk();

    // The key insight: after load, getLastSentToMeshMillis should return non-zero
    // because loadFromDisk seeds lastMillis[key] = millis() for every loaded entry.
    // This ensures throttle works even without RTC.
    uint32_t result = transmitHistory->getLastSentToMeshMillis(meshtastic_PortNum_NODEINFO_APP);

    uint32_t epoch = transmitHistory->getLastSentToMeshEpoch(meshtastic_PortNum_NODEINFO_APP);
    if (epoch > 0) {
        // Data was persisted — millis must be seeded
        TEST_ASSERT_NOT_EQUAL(0, result);

        // And it should cause throttle to block (treating as "just sent")
        bool withinInterval = Throttle::isWithinTimespanMs(result, 10 * 60 * 1000);
        TEST_ASSERT_TRUE(withinInterval);
    }
    // If epoch == 0, RTC wasn't available — no data was saved, so nothing to restore.
    // This is expected on platforms without RTC during the very first boot.
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
    RUN_TEST(test_load_seeds_millis_even_without_rtc);

    exit(UNITY_END());
}

void loop() {}
