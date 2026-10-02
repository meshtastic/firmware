// NodeInfoModule's send window and the routine-broadcast countdown: allocReply(),
// sendOurNodeInfo() and runOnce() in src/modules/NodeInfoModule.cpp.
//
// Two contracts, both properties of the module rather than of the scaler it calls:
//
// 1. The non-interactive window has a 30 minute floor. allocReply() passes 30 * 60 as the base to
//    Default::getConfiguredOrDefaultMsScaled(); what that base becomes at mesh sizes over 40 nodes,
//    per modem preset and per role, is Default's own contract and is covered in test_default -
//    these tests pin the base and leave the multiplier alone. The interactive path (shorterTimeout,
//    used by a user-triggered send, a PKI decrypt failure and a completed key verification) keeps
//    its separate 60 second gate and must not inherit the floor.
//
// 2. A send that goes out re-arms the routine broadcast, and a send that is refused does not.
//    sendOurNodeInfo() calls setIntervalFromNow() with the configured broadcast interval once the
//    packet is queued, so an ad-hoc send is not followed minutes later by the periodic copy. The
//    reset sits on the return-true path deliberately: if a refused send could re-arm it, a node
//    that keeps attempting greetings inside the window would defer its broadcast indefinitely and
//    go silent - the opposite of the intent.
//
// A preset or channel change (radioGeneration) rides on the same path: runOnce() asks for replies
// while currentGeneration != radioGeneration and copies the generation across only on a true
// return, so a refused send has to leave the request pending for the next attempt.
//
// Regressions guarded: reverting the base to 10 * 60, the value this branch replaced; hoisting the
// setIntervalFromNow() call above the veto checks or onto the false path; and moving the generation
// copy out of `if (sendOurNodeInfo(...))`, which loses a preset change to a throttled send so the
// mesh is never asked to re-introduce itself.
//
// The window probes step an injected clock (Time::setTestMillis) rather than sleeping;
// TransmitHistory, which is where allocReply() reads "last sent" from, reads the same clock.
#include "MeshTypes.h" // BEFORE TestUtil.h
#include "TestUtil.h"
#include <unity.h>

#if defined(ARCH_PORTDUINO)
#define NI_TEST_ENTRY extern "C"
#else
#define NI_TEST_ENTRY
#endif

#include "Default.h"
#include "NodeStatus.h"
#include "UptimeClock.h"
#include "airtime.h"
#include "mesh/NodeDB.h"
#include "mesh/RadioInterface.h"
#include "mesh/Router.h"
#include "mesh/TransmitHistory.h"
#include "modules/NodeInfoModule.h"
#include "support/MockMeshService.h"
#include <memory>
#include <vector>

// Exposes the protected periodic entry point. The countdown itself is read through NodeInfoModule's
// own ForTests accessors: OSThread is a private base, so a shim cannot reach it. Reading the interval
// rather than a deadline keeps these assertions off wall-clock millis(), which the test clock does not drive.
class NodeInfoModuleTestShim : public NodeInfoModule
{
  public:
    using NodeInfoModule::runOnce;
};

namespace
{

// sendLocal() is not virtual and refuses to send with no interface attached, so the mock router
// carries a stub one. Only send() and getPacketTime() are pure virtual, and init() - which is what
// observes config and sleep notifications - is never called.
class StubRadioInterface : public RadioInterface
{
  public:
    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        packetPool.release(p);
        return ERRNO_OK;
    }
    uint32_t getPacketTime(uint32_t totalPacketLen, bool received = false) override { return 100; }
};

class MockRouter : public Router
{
  public:
    MockRouter() { addInterface(std::unique_ptr<RadioInterface>(new StubRadioInterface())); }

    ErrorCode send(meshtastic_MeshPacket *p) override
    {
        sentPackets.push_back(*p);
        packetPool.release(p); // released here either way: the interface owns the packet it declined
        return sendResult;
    }

    ErrorCode sendResult = ERRNO_OK;

    // The broadcast loopback copy lands here; release rather than queue into fromRadioQueue, which
    // nothing drains in tests.
    void enqueueReceivedMessage(meshtastic_MeshPacket *p) override { packetPool.release(p); }

    std::vector<meshtastic_MeshPacket> sentPackets;
};

NodeInfoModuleTestShim *mod = nullptr;
MockMeshService *mockSvc = nullptr;
MockRouter *mockRouter = nullptr;
AirTime *testAirTime = nullptr;

constexpr uint32_t kClockBaseMs = 60 * 60 * 1000; // an hour in, so no probe can underflow
constexpr uint32_t kThirtyMinMs = 30 * 60 * 1000;
constexpr uint32_t kThreeHoursMs = 3 * 60 * 60 * 1000;

// Stamp "we sent a NodeInfo just now", jump the clock forward, and report whether another send is
// allowed. A permitted send re-stamps the history, which is why every probe stamps first.
bool sendAllowedAfterMs(uint32_t elapsedMs, bool shorterTimeout = false)
{
    transmitHistory->setLastSentToMesh(meshtastic_PortNum_NODEINFO_APP);
    Time::advanceTestMillis(elapsedMs);
    return mod->sendOurNodeInfo(NODENUM_BROADCAST, false, 0, shorterTimeout);
}

} // namespace

void setUp(void)
{
    Time::resetMonotonicForTests();
    Time::setTestMillis(kClockBaseMs);
    Time::serviceMonotonic();

    testAirTime = new AirTime();
    airTime = testAirTime;

    mockSvc = new MockMeshService();
    service = mockSvc;

    mockRouter = new MockRouter();
    router = mockRouter;

    if (transmitHistory) {
        delete transmitHistory;
        transmitHistory = nullptr;
    }
    transmitHistory = TransmitHistory::getInstance(); // fresh: loadFromDisk() is not called

    // The congestion coefficient is 1.0 at or below 40 online nodes, so the window under test is
    // the bare floor. Nothing wires the node-status observer in a test build, but assert it rather
    // than assume it - a non-zero count here would silently stretch every boundary below.
    TEST_ASSERT_NOT_NULL(nodeStatus);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, nodeStatus->getNumOnline(), "these boundaries assume an unscaled window");

    config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    config.device.node_info_broadcast_secs = 0; // 0 selects the default, 3 hours

    owner.is_licensed = false;
    strncpy(owner.long_name, "send window", sizeof(owner.long_name) - 1);
    strncpy(owner.short_name, "sw", sizeof(owner.short_name) - 1);

    radioGeneration = 1;
    mod = new NodeInfoModuleTestShim();
    nodeInfoModule = mod;

    // Settle the startup generation so no test inherits a pending "ask for replies", then drop the
    // stamp that settling send left behind: every test starts unthrottled, and the ones that need a
    // refusal arm the floor themselves.
    mod->runOnce();
    mockRouter->sentPackets.clear();
    delete transmitHistory;
    transmitHistory = nullptr;
    transmitHistory = TransmitHistory::getInstance();
}

void tearDown(void)
{
    nodeInfoModule = nullptr;
    delete mod;
    mod = nullptr;

    // sendToMesh() copies a queue status to the phone on every send; toPhoneQueue takes ownership
    // and nothing else drains it, so release them or LeakSanitizer aborts the run.
    if (mockSvc) {
        meshtastic_MeshPacket *p;
        while ((p = mockSvc->getForPhone()) != nullptr)
            mockSvc->releaseToPool(p);
    }

    service = nullptr;
    delete mockSvc;
    mockSvc = nullptr;

    router = nullptr;
    delete mockRouter;
    mockRouter = nullptr;

    airTime = nullptr;
    delete testAirTime;
    testAirTime = nullptr;

    delete transmitHistory;
    transmitHistory = nullptr;

    Time::useRealClock();
}

// The floor is 30 minutes, not the 10 it used to be: 10 and 29:59 are refused, 30:01 is not.
static void test_sendWindow_floorIsThirtyMinutes(void)
{
    TEST_ASSERT_FALSE_MESSAGE(sendAllowedAfterMs(10 * 60 * 1000), "10 min must be inside the window");
    TEST_ASSERT_FALSE_MESSAGE(sendAllowedAfterMs(kThirtyMinMs - 1000), "29:59 must be inside the window");
    TEST_ASSERT_TRUE_MESSAGE(sendAllowedAfterMs(kThirtyMinMs + 1000), "30:01 must be past the window");
}

// The interactive path keeps its own 60 second gate. Raising the routine floor must not have raised
// it, or a user-triggered send and a key verification would wait out half an hour.
static void test_sendWindow_interactiveSendKeepsItsSixtySecondGate(void)
{
    TEST_ASSERT_FALSE_MESSAGE(sendAllowedAfterMs(30 * 1000, /*shorterTimeout=*/true), "30 s is inside the 60 s gate");
    TEST_ASSERT_TRUE_MESSAGE(sendAllowedAfterMs(61 * 1000, /*shorterTimeout=*/true), "61 s is past the 60 s gate");
    TEST_ASSERT_TRUE_MESSAGE(sendAllowedAfterMs(5 * 60 * 1000, /*shorterTimeout=*/true),
                             "5 min must pass the interactive gate while still inside the routine floor");
}

// A send that goes out re-arms the countdown to a full interval - the default, and the configured
// value when there is one. Arming 1 ms first means only the reset can produce the expected value.
static void test_broadcastTimer_aSendRearmsTheRoutineCountdown(void)
{
    mod->armBroadcastCountdownForTests(1);
    TEST_ASSERT_TRUE(mod->sendOurNodeInfo(NODENUM_BROADCAST, false, 0, false));
    TEST_ASSERT_EQUAL_UINT32(Default::getConfiguredOrDefaultMs(0, default_node_info_broadcast_secs),
                             (uint32_t)mod->broadcastCountdownMsForTests());
    TEST_ASSERT_EQUAL_UINT32(kThreeHoursMs, (uint32_t)mod->broadcastCountdownMsForTests());

    config.device.node_info_broadcast_secs = 4 * 60 * 60;
    mod->armBroadcastCountdownForTests(1);
    Time::advanceTestMillis(kThirtyMinMs + 1000);
    TEST_ASSERT_TRUE(mod->sendOurNodeInfo(NODENUM_BROADCAST, false, 0, false));
    TEST_ASSERT_EQUAL_UINT32(4 * 60 * 60 * 1000, (uint32_t)mod->broadcastCountdownMsForTests());
}

// interval is not what the scheduler reads: shouldRun() keys off _cached_next_run, and
// Thread::setInterval() recomputes that from last_run while setIntervalFromNow() recomputes it from
// now. Age last_run by an hour first and the two answers differ by an hour, so this case fails if
// the send ever re-arms the period without moving the deadline - which would fire the routine copy
// straight after an ad-hoc send, the exact thing the reset exists to prevent.
static void test_broadcastTimer_aSendMovesTheDeadlineNotJustThePeriod(void)
{
    const unsigned long ageMs = 60 * 60 * 1000;
    mod->ageLastRunForTests(ageMs);
    TEST_ASSERT_TRUE(mod->sendOurNodeInfo(NODENUM_BROADCAST, false, 0, false));

    const unsigned long remaining = mod->broadcastDeadlineMsForTests() - millis();
    TEST_ASSERT_UINT32_WITHIN_MESSAGE(5000, kThreeHoursMs, (uint32_t)remaining,
                                      "the next routine broadcast is due a full interval from the send, not from the last tick");
}

// An ad-hoc unicast - the shape of a greeting, a PKI decrypt failure or a completed key
// verification - re-arms the countdown just as a broadcast does. Without it the routine copy
// follows the ad-hoc one within minutes, putting two NodeInfos on the air for no gain.
static void test_broadcastTimer_anAdHocUnicastRearmsItToo(void)
{
    mod->armBroadcastCountdownForTests(1);

    TEST_ASSERT_TRUE(mod->sendOurNodeInfo(0x12345678, true, 0, false));
    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
    TEST_ASSERT_EQUAL_HEX32(0x12345678, mockRouter->sentPackets[0].to);
    TEST_ASSERT_EQUAL_UINT32(kThreeHoursMs, (uint32_t)mod->broadcastCountdownMsForTests());
}

// A refused send must leave the countdown exactly where it was, or a node that keeps attempting
// greetings inside the window defers its routine broadcast forever.
static void test_broadcastTimer_aRefusedSendLeavesTheCountdownAlone(void)
{
    transmitHistory->setLastSentToMesh(meshtastic_PortNum_NODEINFO_APP);
    Time::advanceTestMillis(60 * 1000); // a minute later: well inside the floor

    const unsigned long sentinel = 4321;
    mod->armBroadcastCountdownForTests(sentinel);

    TEST_ASSERT_FALSE_MESSAGE(mod->sendOurNodeInfo(NODENUM_BROADCAST, false, 0, false), "the floor must refuse this send");
    TEST_ASSERT_EQUAL_UINT32(0, mockRouter->sentPackets.size());
    TEST_ASSERT_EQUAL_UINT32(sentinel, (uint32_t)mod->broadcastCountdownMsForTests());
}

// A licensed station announces its call sign on a regulatory interval - ham mode sets
// node_info_broadcast_secs to 600 s for the FCC minimum - and the floor must not stretch that to 30
// minutes. The unlicensed control below is the same configuration without the licence, so the
// assertion cannot pass by the floor quietly disappearing for everyone.
static void test_sendWindow_aLicensedStationKeepsItsCallSignInterval(void)
{
    config.device.node_info_broadcast_secs = 600;

    owner.is_licensed = true;
    TEST_ASSERT_FALSE_MESSAGE(sendAllowedAfterMs(5 * 60 * 1000), "5 min is inside the station's own 10 min interval");
    TEST_ASSERT_TRUE_MESSAGE(sendAllowedAfterMs(11 * 60 * 1000), "11 min is past it, and the floor must not override it");

    owner.is_licensed = false;
    TEST_ASSERT_FALSE_MESSAGE(sendAllowedAfterMs(11 * 60 * 1000), "without a licence the 30 minute floor still applies");
}

// A send the router declines never reached the air. It must not defer the routine broadcast, and it
// must report failure so runOnce() does not treat a pending channel change as delivered.
static void test_broadcastTimer_aRejectedSendLeavesTheCountdownAlone(void)
{
    const unsigned long sentinel = 8765;
    mod->armBroadcastCountdownForTests(sentinel);
    mockRouter->sendResult = ERRNO_NO_INTERFACES;

    TEST_ASSERT_FALSE_MESSAGE(mod->sendOurNodeInfo(NODENUM_BROADCAST, false, 0, false), "a declined send is not a send");
    TEST_ASSERT_EQUAL_UINT32(sentinel, (uint32_t)mod->broadcastCountdownMsForTests());
}

// The countdown is only half of it: allocReply() used to stamp TransmitHistory when it built the
// packet, so a send the router then declined still started the window. With a 30 minute floor that
// silences the node for half an hour over a packet that never left. The retry immediately after must
// go out.
static void test_sendWindow_aRejectedSendDoesNotStartTheWindow(void)
{
    mockRouter->sendResult = ERRNO_NO_INTERFACES;
    TEST_ASSERT_FALSE_MESSAGE(mod->sendOurNodeInfo(NODENUM_BROADCAST, false, 0, false), "the router declined this one");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, transmitHistory->getLastSentToMeshMillis(meshtastic_PortNum_NODEINFO_APP),
                                     "a declined send must leave no transmit stamp behind");

    mockRouter->sendResult = ERRNO_OK;
    mockRouter->sentPackets.clear();
    TEST_ASSERT_TRUE_MESSAGE(mod->sendOurNodeInfo(NODENUM_BROADCAST, false, 0, false),
                             "the retry must not be throttled by the send that failed");
    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
}

// A preset or channel change bumps radioGeneration, and only a send that goes out consumes it: the
// refused attempt leaves the ask pending, the next successful one carries want_response, and the
// one after that does not ask again.
static void test_presetChange_isConsumedOnlyByASendThatGoesOut(void)
{
    radioGeneration++;

    // Arm the floor so the first attempt is refused, which is the case under test.
    transmitHistory->setLastSentToMesh(meshtastic_PortNum_NODEINFO_APP);
    Time::advanceTestMillis(60 * 1000);

    mod->runOnce();
    TEST_ASSERT_EQUAL_UINT32(0, mockRouter->sentPackets.size());

    Time::advanceTestMillis(kThirtyMinMs + 1000);
    mod->runOnce();
    TEST_ASSERT_EQUAL_UINT32(1, mockRouter->sentPackets.size());
    TEST_ASSERT_TRUE_MESSAGE(mockRouter->sentPackets[0].decoded.want_response,
                             "a refused send must not consume the preset change");
    TEST_ASSERT_EQUAL_HEX32(NODENUM_BROADCAST, mockRouter->sentPackets[0].to);

    Time::advanceTestMillis(kThirtyMinMs + 1000);
    mod->runOnce();
    TEST_ASSERT_EQUAL_UINT32(2, mockRouter->sentPackets.size());
    TEST_ASSERT_FALSE_MESSAGE(mockRouter->sentPackets[1].decoded.want_response,
                              "a settled generation must not keep asking for replies");
}

NI_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    nodeDB = new NodeDB();

    UNITY_BEGIN();
    RUN_TEST(test_sendWindow_floorIsThirtyMinutes);
    RUN_TEST(test_sendWindow_interactiveSendKeepsItsSixtySecondGate);
    RUN_TEST(test_broadcastTimer_aSendRearmsTheRoutineCountdown);
    RUN_TEST(test_broadcastTimer_aSendMovesTheDeadlineNotJustThePeriod);
    RUN_TEST(test_broadcastTimer_anAdHocUnicastRearmsItToo);
    RUN_TEST(test_broadcastTimer_aRefusedSendLeavesTheCountdownAlone);
    RUN_TEST(test_broadcastTimer_aRejectedSendLeavesTheCountdownAlone);
    RUN_TEST(test_sendWindow_aRejectedSendDoesNotStartTheWindow);
    RUN_TEST(test_sendWindow_aLicensedStationKeepsItsCallSignInterval);
    RUN_TEST(test_presetChange_isConsumedOnlyByASendThatGoesOut);
    exit(UNITY_END());
}
NI_TEST_ENTRY void loop() {}
