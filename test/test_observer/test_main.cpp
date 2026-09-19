// Unit tests for src/Observer.h: notification order, the nonzero-return abort chain,
// CallbackObserver dispatch, ~Observer auto-detach, and list mutation from inside onNotify.
#include "Arduino.h"
#include "Observer.h"
#include "TestUtil.h"
#include <cstdio>
#include <string>
#include <unity.h>

// Tags of observers in the order their onNotify ran, e.g. "ABC". Cleared in setUp.
static std::string callOrder;

// An observer that records its calls and can optionally mutate observer lists from inside
// onNotify - the mid-notify hazard the detach/attach-during-notify tests drive.
class RecordingObserver : public Observer<int>
{
  public:
    explicit RecordingObserver(char _tag) : tag(_tag) {}

    char tag;
    int returnCode = 0;
    int calls = 0;
    int lastArg = 0;

    // When set, onNotify detaches detachWho from detachFrom before returning.
    Observer<int> *detachWho = nullptr;
    Observable<int> *detachFrom = nullptr;

    // When set, onNotify attaches attachWho to attachTo before returning.
    Observer<int> *attachWho = nullptr;
    Observable<int> *attachTo = nullptr;

  protected:
    int onNotify(int arg) override
    {
        callOrder += tag;
        calls++;
        lastArg = arg;
        if (detachWho && detachFrom)
            detachWho->unobserve(detachFrom);
        if (attachWho && attachTo)
            attachWho->observe(attachTo);
        return returnCode;
    }
};

// Target class for the CallbackObserver member-pointer dispatch tests.
class CallbackTarget
{
  public:
    int calls = 0;
    int lastArg = 0;

    int handle(int arg)
    {
        calls++;
        lastArg = arg;
        return 0;
    }

    int handleAbort(int arg)
    {
        calls++;
        lastArg = arg;
        return 42;
    }
};

// --- basic delivery ---

void test_notify_with_no_observers_returns_zero()
{
    Observable<int> subject;
    TEST_ASSERT_EQUAL(0, subject.notifyObservers(99));
}

void test_notify_order_and_arg()
{
    Observable<int> subject;
    RecordingObserver a('A'), b('B'), c('C');
    a.observe(&subject);
    b.observe(&subject);
    c.observe(&subject);

    TEST_ASSERT_EQUAL(0, subject.notifyObservers(42));
    TEST_ASSERT_EQUAL_STRING("ABC", callOrder.c_str()); // insertion order
    TEST_ASSERT_EQUAL(42, a.lastArg);
    TEST_ASSERT_EQUAL(42, b.lastArg);
    TEST_ASSERT_EQUAL(42, c.lastArg);

    // Delivery is not one-shot: a second notify reaches everyone again.
    TEST_ASSERT_EQUAL(0, subject.notifyObservers(43));
    TEST_ASSERT_EQUAL_STRING("ABCABC", callOrder.c_str());
    TEST_ASSERT_EQUAL(2, b.calls);
    TEST_ASSERT_EQUAL(43, b.lastArg);
}

// --- abort contract ---

void test_nonzero_return_aborts_chain_and_propagates()
{
    Observable<int> subject;
    RecordingObserver a('A'), b('B'), c('C');
    a.observe(&subject);
    b.observe(&subject);
    c.observe(&subject);

    b.returnCode = 7;
    TEST_ASSERT_EQUAL(7, subject.notifyObservers(1));
    TEST_ASSERT_EQUAL_STRING("AB", callOrder.c_str());
    TEST_ASSERT_EQUAL(0, c.calls); // chain stopped before C

    // Clearing the abort restores full delivery.
    b.returnCode = 0;
    callOrder.clear();
    TEST_ASSERT_EQUAL(0, subject.notifyObservers(2));
    TEST_ASSERT_EQUAL_STRING("ABC", callOrder.c_str());
}

// --- CallbackObserver ---

void test_callback_observer_dispatches_member_function()
{
    Observable<int> subject;
    CallbackTarget target;
    CallbackObserver<CallbackTarget, int> cb(&target, &CallbackTarget::handle);
    cb.observe(&subject);

    TEST_ASSERT_EQUAL(0, subject.notifyObservers(1234));
    TEST_ASSERT_EQUAL(1, target.calls);
    TEST_ASSERT_EQUAL(1234, target.lastArg);
}

void test_callback_observer_return_code_aborts_chain()
{
    Observable<int> subject;
    CallbackTarget target;
    CallbackObserver<CallbackTarget, int> cb(&target, &CallbackTarget::handleAbort);
    RecordingObserver after('X');
    cb.observe(&subject);
    after.observe(&subject);

    TEST_ASSERT_EQUAL(42, subject.notifyObservers(5));
    TEST_ASSERT_EQUAL(1, target.calls);
    TEST_ASSERT_EQUAL(0, after.calls); // callback's abort code stopped the chain
}

// --- lifecycle: destructor auto-detach ---

void test_destroyed_observer_is_not_notified()
{
    Observable<int> subject;
    RecordingObserver a('A'), c('C');
    a.observe(&subject);
    RecordingObserver *b = new RecordingObserver('B');
    b->observe(&subject);
    c.observe(&subject);

    TEST_ASSERT_EQUAL(0, subject.notifyObservers(1));
    TEST_ASSERT_EQUAL_STRING("ABC", callOrder.c_str());

    delete b; // ~Observer must remove it from the observable's list

    callOrder.clear();
    TEST_ASSERT_EQUAL(0, subject.notifyObservers(2)); // ASan-clean: no dangling pointer left behind
    TEST_ASSERT_EQUAL_STRING("AC", callOrder.c_str());
}

void test_observer_watching_two_observables_detaches_from_both()
{
    Observable<int> subject1;
    Observable<int> subject2;
    {
        RecordingObserver x('X');
        x.observe(&subject1);
        x.observe(&subject2); // re-target onto a second observable: both now deliver
        subject1.notifyObservers(1);
        subject2.notifyObservers(2);
        TEST_ASSERT_EQUAL(2, x.calls);
        TEST_ASSERT_EQUAL(2, x.lastArg);
    } // x destroyed here - must have detached from both observables

    callOrder.clear();
    TEST_ASSERT_EQUAL(0, subject1.notifyObservers(3));
    TEST_ASSERT_EQUAL(0, subject2.notifyObservers(4));
    TEST_ASSERT_EQUAL_STRING("", callOrder.c_str());
}

// --- duplicate observe / unobserve semantics ---

void test_duplicate_observe_delivers_twice_and_unobserve_removes_all()
{
    Observable<int> subject;
    RecordingObserver a('A');
    a.observe(&subject);
    a.observe(&subject); // current semantics: second observe means double delivery

    TEST_ASSERT_EQUAL(0, subject.notifyObservers(9));
    TEST_ASSERT_EQUAL_STRING("AA", callOrder.c_str());
    TEST_ASSERT_EQUAL(2, a.calls);

    // One unobserve removes every entry (std::list::remove semantics), not just one.
    a.unobserve(&subject);
    callOrder.clear();
    TEST_ASSERT_EQUAL(0, subject.notifyObservers(10));
    TEST_ASSERT_EQUAL_STRING("", callOrder.c_str());
    TEST_ASSERT_EQUAL(2, a.calls);
}

void test_unobserve_of_never_observed_observable_is_noop()
{
    Observable<int> subject;
    RecordingObserver a('A'), stranger('S');
    a.observe(&subject);

    stranger.unobserve(&subject); // never attached: must be a safe no-op

    TEST_ASSERT_EQUAL(0, subject.notifyObservers(1));
    TEST_ASSERT_EQUAL_STRING("A", callOrder.c_str());
    TEST_ASSERT_EQUAL(0, stranger.calls);
}

// --- list mutation from inside onNotify (the safe cases) ---

void test_detach_of_earlier_observer_during_notify()
{
    Observable<int> subject;
    RecordingObserver a('A'), b('B'), c('C');
    a.observe(&subject);
    b.observe(&subject);
    c.observe(&subject);
    b.detachWho = &a; // B removes already-visited A mid-notify
    b.detachFrom = &subject;

    TEST_ASSERT_EQUAL(0, subject.notifyObservers(1));
    TEST_ASSERT_EQUAL_STRING("ABC", callOrder.c_str()); // A was visited before removal; C unaffected

    b.detachWho = nullptr;
    callOrder.clear();
    TEST_ASSERT_EQUAL(0, subject.notifyObservers(2));
    TEST_ASSERT_EQUAL_STRING("BC", callOrder.c_str()); // A stays detached
}

void test_detach_of_later_observer_during_notify()
{
    Observable<int> subject;
    RecordingObserver a('A'), b('B'), c('C');
    a.observe(&subject);
    b.observe(&subject);
    c.observe(&subject);
    a.detachWho = &c; // A removes not-yet-visited C mid-notify
    a.detachFrom = &subject;

    TEST_ASSERT_EQUAL(0, subject.notifyObservers(1));
    TEST_ASSERT_EQUAL_STRING("AB", callOrder.c_str()); // iteration stays valid, C never called
    TEST_ASSERT_EQUAL(0, c.calls);

    a.detachWho = nullptr;
    callOrder.clear();
    TEST_ASSERT_EQUAL(0, subject.notifyObservers(2));
    TEST_ASSERT_EQUAL_STRING("AB", callOrder.c_str());
}

// Tightest safe case: removing the node the iterator will step to next. std::list relinks A's
// next pointer when B's node is erased, so ++iterator lands on C.
void test_detach_of_immediately_next_observer_during_notify()
{
    Observable<int> subject;
    RecordingObserver a('A'), b('B'), c('C');
    a.observe(&subject);
    b.observe(&subject);
    c.observe(&subject);
    a.detachWho = &b;
    a.detachFrom = &subject;

    TEST_ASSERT_EQUAL(0, subject.notifyObservers(1));
    TEST_ASSERT_EQUAL_STRING("AC", callOrder.c_str());
    TEST_ASSERT_EQUAL(0, b.calls);

    a.detachWho = nullptr;
    callOrder.clear();
    TEST_ASSERT_EQUAL(0, subject.notifyObservers(2));
    TEST_ASSERT_EQUAL_STRING("AC", callOrder.c_str());
}

// Self-detach is only safe when the observer also aborts the chain: returning nonzero exits
// before the iterator is advanced past the node unobserve() just erased. PhoneAPI is the one
// observer in the tree that does this (onNotify -> checkConnectionTimeout -> close() ->
// unobserve, returning -1), and its -1 is load-bearing, not incidental. A self-detaching
// observer that returned 0 would walk a freed node - not covered here, because asserting that
// would be asserting UB; notifyObservers() has to be hardened before it can be tested.
void test_self_detach_with_abort_during_notify()
{
    Observable<int> subject;
    RecordingObserver a('A'), b('B'), c('C');
    a.observe(&subject);
    b.observe(&subject);
    c.observe(&subject);
    b.detachWho = &b;
    b.detachFrom = &subject;
    b.returnCode = -1;

    TEST_ASSERT_EQUAL(-1, subject.notifyObservers(1));
    TEST_ASSERT_EQUAL_STRING("AB", callOrder.c_str()); // C never runs: the chain aborted
    TEST_ASSERT_EQUAL(0, c.calls);

    b.detachWho = nullptr;
    b.returnCode = 0;
    callOrder.clear();
    TEST_ASSERT_EQUAL(0, subject.notifyObservers(2));
    TEST_ASSERT_EQUAL_STRING("AC", callOrder.c_str());
}

void test_attach_during_notify_is_safe_and_delivers_next_time()
{
    Observable<int> subject;
    RecordingObserver a('A'), b('B'), c('C'), d('D');
    a.observe(&subject);
    b.observe(&subject);
    c.observe(&subject);
    a.attachWho = &d; // A appends D mid-notify (push_back never invalidates list iterators)
    a.attachTo = &subject;

    TEST_ASSERT_EQUAL(0, subject.notifyObservers(1));
    // The pre-existing observers all ran, in order. Whether the same pass also reaches the
    // freshly appended D is deliberately not asserted - a hardened notifyObservers that
    // snapshots the list would legitimately change that, and it should not go red for it.
    TEST_ASSERT_EQUAL_STRING("ABC", callOrder.substr(0, 3).c_str());

    a.attachWho = nullptr;
    callOrder.clear();
    TEST_ASSERT_EQUAL(0, subject.notifyObservers(2));
    TEST_ASSERT_EQUAL_STRING("ABCD", callOrder.c_str()); // D is a full participant from now on
}

// --- Unity lifecycle ---

void setUp(void)
{
    callOrder.clear();
}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();

    printf("\n=== Basic delivery ===\n");
    RUN_TEST(test_notify_with_no_observers_returns_zero);
    RUN_TEST(test_notify_order_and_arg);

    printf("\n=== Abort contract ===\n");
    RUN_TEST(test_nonzero_return_aborts_chain_and_propagates);

    printf("\n=== CallbackObserver ===\n");
    RUN_TEST(test_callback_observer_dispatches_member_function);
    RUN_TEST(test_callback_observer_return_code_aborts_chain);

    printf("\n=== Lifecycle ===\n");
    RUN_TEST(test_destroyed_observer_is_not_notified);
    RUN_TEST(test_observer_watching_two_observables_detaches_from_both);

    printf("\n=== Duplicate observe / unobserve ===\n");
    RUN_TEST(test_duplicate_observe_delivers_twice_and_unobserve_removes_all);
    RUN_TEST(test_unobserve_of_never_observed_observable_is_noop);

    printf("\n=== Mutation during notify (safe cases) ===\n");
    RUN_TEST(test_detach_of_earlier_observer_during_notify);
    RUN_TEST(test_detach_of_later_observer_during_notify);
    RUN_TEST(test_detach_of_immediately_next_observer_during_notify);
    RUN_TEST(test_self_detach_with_abort_during_notify);
    RUN_TEST(test_attach_during_notify_is_safe_and_delivers_next_time);

    exit(UNITY_END());
}

void loop() {}
