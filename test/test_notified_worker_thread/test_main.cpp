// Unit tests for NotifiedWorkerThread::checkNotificationExcept() (src/concurrency/NotifiedWorkerThread.cpp).
//
// Every radio driver's trySetStandby() calls it with TRANSMIT_DELAY_COMPLETED before forcing standby, so that a
// pending radio interrupt is handled first but a transmit whose backoff timer is still running is not. notifyLater()
// stores its value at once and only delays the wake-up, so the old checkNotification() there ran that transmit
// early, from inside the standby, and the standby then cut it off a few ms into its airtime with its payload still
// in the radio. On an LR2021 the unsent payload stayed in the TX FIFO and went out ahead of every later packet.
//
// The regression guarded: a deferred notification being consumed (run early, or lost) by checkNotificationExcept(),
// or an interrupt notification being left behind by it.
#include "TestUtil.h"
#include "concurrency/NotifiedWorkerThread.h"
#include <unity.h>
#include <vector>

namespace
{
constexpr uint32_t IRQ_EVENT = 1;
constexpr uint32_t DEFERRED_EVENT = 3;

class RecordingWorker : public concurrency::NotifiedWorkerThread
{
  public:
    RecordingWorker() : NotifiedWorkerThread("RecordingWorker") {}
    std::vector<uint32_t> seen;
    void checkExcept(uint32_t deferred) { checkNotificationExcept(deferred); }
    void check() { checkNotification(); }

  protected:
    void onNotify(uint32_t notification) override { seen.push_back(notification); }
};

// Static: OSThread's ctor registers `this` with the global ThreadController, and Unity's longjmp skips dtors.
RecordingWorker &worker()
{
    static RecordingWorker w;
    return w;
}
} // namespace

void setUp(void)
{
    worker().check(); // drain whatever the previous case left
    worker().seen.clear();
}
void tearDown(void) {}

static void test_checkNotificationExcept_leavesDeferredPending()
{
    worker().notifyLater(60000, DEFERRED_EVENT, true);
    worker().checkExcept(DEFERRED_EVENT);
    TEST_ASSERT_EQUAL_UINT32(0, worker().seen.size());

    // Still there for its own wake-up
    worker().check();
    TEST_ASSERT_EQUAL_UINT32(1, worker().seen.size());
    TEST_ASSERT_EQUAL_UINT32(DEFERRED_EVENT, worker().seen[0]);
}

static void test_checkNotificationExcept_runsOtherNotifications()
{
    worker().notify(IRQ_EVENT, true);
    worker().checkExcept(DEFERRED_EVENT);
    TEST_ASSERT_EQUAL_UINT32(1, worker().seen.size());
    TEST_ASSERT_EQUAL_UINT32(IRQ_EVENT, worker().seen[0]);

    // Consumed: a second look finds nothing
    worker().check();
    TEST_ASSERT_EQUAL_UINT32(1, worker().seen.size());
}

static void test_checkNotificationExcept_nothingPending()
{
    worker().checkExcept(DEFERRED_EVENT);
    TEST_ASSERT_EQUAL_UINT32(0, worker().seen.size());
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_checkNotificationExcept_leavesDeferredPending);
    RUN_TEST(test_checkNotificationExcept_runsOtherNotifications);
    RUN_TEST(test_checkNotificationExcept_nothingPending);
    exit(UNITY_END());
}

void loop() {}
