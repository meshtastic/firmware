// Unit tests for OSThread::currentThread's lifetime
// (src/concurrency/OSThread.cpp).
//
// currentThread is the only attribution a log line carries:
// RedirectablePrint::log reads currentThread->ThreadName and prints it as the
// "[Tag] " prefix. OSThread::run() points it at the running thread for the
// whole of runOnce() and clears it afterwards, so during a runOnce() the
// pointer is a borrowed reference to an object that runOnce() is itself allowed
// to destroy - and the simulator does: exit_simulator calls exit(0) from
// AdminModule while ServerAPI is the running thread, which runs ServerAPI's
// destructor with currentThread still pointing at it.
//
// The regression guarded: a destroyed thread left reachable through
// currentThread. Bases are destroyed in reverse declaration order, so for
// `ServerAPI : public StreamAPI, private OSThread` the Thread base frees
// ThreadName first and PhoneAPI::~PhoneAPI's own LOG_DEBUG then reads it.
// AddressSanitizer caught that as a heap-use-after-free on the native
// simulator's exit path, in a CI run that still concluded green. The destructor
// has to drop the pointer along with the name it refers to; the log call after
// the delete below is the read that faulted, so a regression reproduces here
// under the ASan-linked coverage env and not only in the simulator.
#include "configuration.h"

#include "TestUtil.h"
#include "concurrency/OSThread.h"
#include <unity.h>

namespace
{
class ProbeThread : public concurrency::OSThread
{
  public:
    ProbeThread() : OSThread("ProbeThread") {}
    const concurrency::OSThread *seenWhileRunning = nullptr;
    void runNow() { run(); }

  protected:
    int32_t runOnce() override
    {
        seenWhileRunning = currentThread;
        return 1000;
    }
};
} // namespace

void setUp(void)
{
    concurrency::OSThread::currentThread = nullptr;
}
void tearDown(void)
{
    concurrency::OSThread::currentThread = nullptr;
}

// The mechanism the destructor has to undo: inside runOnce() the pointer is
// this thread, and run() clears it on the way out, so a thread that is not
// running is never the current one.
static void test_run_points_currentThread_at_the_running_thread()
{
    ProbeThread probe;

    probe.runNow();
    TEST_ASSERT_EQUAL_PTR(&probe, probe.seenWhileRunning);
    TEST_ASSERT_NULL(concurrency::OSThread::currentThread);
}

// Destroying the running thread - what exit_simulator does from inside
// ServerAPI's runOnce - must leave nothing behind to read. The LOG_DEBUG is
// that read: it goes through RedirectablePrint::log, which dereferences
// currentThread for the tag.
static void test_destroying_the_running_thread_clears_currentThread()
{
    auto *probe = new ProbeThread();
    concurrency::OSThread::currentThread = probe; // as run() has it, mid-runOnce()

    delete probe;

    TEST_ASSERT_NULL(concurrency::OSThread::currentThread);
    LOG_DEBUG("logging after the running thread was destroyed");
}

// A thread destroyed while some *other* thread is running must not clear the
// pointer: that would drop the running thread's tag from every later log line
// in the same runOnce().
static void test_destroying_another_thread_leaves_currentThread_alone()
{
    ProbeThread running;
    auto *other = new ProbeThread();
    concurrency::OSThread::currentThread = &running;

    delete other;

    TEST_ASSERT_EQUAL_PTR(&running, concurrency::OSThread::currentThread);
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_run_points_currentThread_at_the_running_thread);
    RUN_TEST(test_destroying_the_running_thread_clears_currentThread);
    RUN_TEST(test_destroying_another_thread_leaves_currentThread_alone);
    exit(UNITY_END());
}

void loop() {}
