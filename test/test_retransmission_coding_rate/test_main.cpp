/**
 * Tests for retransmission coding-rate selection and airtime-calculation
 * ordering in RadioLibInterface::completeSending().
 *
 * Coverage:
 *  1. computeDesiredRetransmissionCodingRate() — the CR escalation policy.
 *  2. completeSending() CR ordering — getPacketTime() must query the hardware
 *     BEFORE restoreTemporaryCodingRateOverride() resets the radio.  This
 *     regression was fixed by reordering the two calls.
 *  3. completeSending() counter semantics — txGood is incremented for every
 *     completed send; txRelay is incremented only when the packet originated
 *     from a remote node (i.e. we were relaying it).
 *  4. completeSending() null-packet guard — even when sendingPacket is null
 *     (spurious interrupt), an active CR override must still be cleared so
 *     the radio is not left stuck at the override CR.
 */

#include "MeshTypes.h"
#include "NextHopRouter.h"
#include "NodeDB.h"
#include "TestRadioLibInterface.h" // test double + wrappers
#include "TestUtil.h"
#include "airtime.h"
#include <unity.h>

// ---------------------------------------------------------------------------
// Node address constants
// ---------------------------------------------------------------------------

static constexpr NodeNum kLocalNodeNum = 0xAAAAAAAA;
static constexpr NodeNum kRemoteNodeNum = 0xBBBBBBBB;

// ---------------------------------------------------------------------------
// Minimal NodeDB stub
//
// NodeDB is not abstract; subclassing gives us a concrete object whose
// getNodeNum() returns myNodeInfo.my_node_num, which we set in setUp().
// The base constructor calls loadFromDisk(), which is a no-op (returns with
// an empty store) in the native test environment.
// ---------------------------------------------------------------------------

class MinimalMockNodeDB : public NodeDB
{
};

// ---------------------------------------------------------------------------
// Test globals
// ---------------------------------------------------------------------------

static TestRadioLibInterface *testIface = nullptr;
static AirTime *savedAirTime = nullptr;
static AirTime *testAirTime = nullptr;
static MinimalMockNodeDB *mockNodeDB = nullptr;

// ---------------------------------------------------------------------------
// Packet factory helper
// ---------------------------------------------------------------------------

/**
 * Allocate a zeroed MeshPacket from the real pool with the given from/to
 * addresses and a small encrypted payload.  completeSending() releases it
 * back to the pool; callers must NOT release it themselves.
 */
static meshtastic_MeshPacket *allocPacket(NodeNum from, NodeNum to)
{
    meshtastic_MeshPacket *p = packetPool.allocZeroed();
    p->from = from;
    p->to = to;
    p->which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    p->encrypted.size = 10;
    return p;
}

// ---------------------------------------------------------------------------
// Unity lifecycle
// ---------------------------------------------------------------------------

void setUp(void)
{
    // AirTime::logAirtime() must not dereference a null pointer.
    savedAirTime = airTime;
    testAirTime = new AirTime();
    airTime = testAirTime;

    // Give isFromUs() a concrete local node number to compare against.
    myNodeInfo.my_node_num = kLocalNodeNum;
    nodeDB = mockNodeDB;

    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    initRegion();

    testIface = new TestRadioLibInterface();
}

void tearDown(void)
{
    delete testIface;
    testIface = nullptr;

    nodeDB = nullptr;

    airTime = savedAirTime;
    delete testAirTime;
    testAirTime = nullptr;
}

// ---------------------------------------------------------------------------
// Section 1 — computeDesiredRetransmissionCodingRate() policy
// ---------------------------------------------------------------------------

static void test_base5_progression()
{
    TEST_ASSERT_EQUAL_UINT8(6, computeDesiredRetransmissionCodingRate(5, 1));
    TEST_ASSERT_EQUAL_UINT8(8, computeDesiredRetransmissionCodingRate(5, 2));
}

static void test_base6_progression()
{
    TEST_ASSERT_EQUAL_UINT8(7, computeDesiredRetransmissionCodingRate(6, 1));
    TEST_ASSERT_EQUAL_UINT8(8, computeDesiredRetransmissionCodingRate(6, 2));
}

static void test_base7_or_higher_progression()
{
    TEST_ASSERT_EQUAL_UINT8(8, computeDesiredRetransmissionCodingRate(7, 1));
    TEST_ASSERT_EQUAL_UINT8(8, computeDesiredRetransmissionCodingRate(7, 2));
    TEST_ASSERT_EQUAL_UINT8(8, computeDesiredRetransmissionCodingRate(8, 1));
    TEST_ASSERT_EQUAL_UINT8(8, computeDesiredRetransmissionCodingRate(8, 2));
}

static void test_second_or_later_retransmission_forces_eight()
{
    TEST_ASSERT_EQUAL_UINT8(8, computeDesiredRetransmissionCodingRate(5, 3));
    TEST_ASSERT_EQUAL_UINT8(8, computeDesiredRetransmissionCodingRate(6, 4));
}

// ---------------------------------------------------------------------------
// Section 2 — completeSending() CR ordering (regression)
// ---------------------------------------------------------------------------

/**
 * Own-originating packet with an active CR override (simulating the second or
 * later retransmission of a packet).  getPacketTime() must see the OVERRIDE
 * CR — not the base CR that restoreTemporaryCodingRateOverride() restores
 * afterward.
 *
 * Regression: the old code called restoreTemporaryCodingRateOverride() first,
 * so getPacketTime() always saw the base CR.
 */
static void test_completeSending_airtimeUsesOverrideCr()
{
    const uint8_t baseCr = 5;
    const uint8_t overrideCr = 7;

    testIface->setBaseCr(baseCr);
    testIface->lastAppliedCr = overrideCr;
    testIface->setActiveTxCodingRateOverrideForTest(overrideCr);

    // from == 0  ⟹  isFromUs() short-circuits to true (own-node packet).
    meshtastic_MeshPacket *p = allocPacket(0, kRemoteNodeNum);
    testIface->setActivePacket(p);
    testIface->triggerCompleteSending();

    TEST_ASSERT_EQUAL_UINT8(overrideCr, testIface->crSeenByGetPacketTime);
    TEST_ASSERT_EQUAL_UINT8(baseCr, testIface->lastAppliedCr);
    TEST_ASSERT_EQUAL_UINT32(1, testIface->txGood);
    TEST_ASSERT_EQUAL_UINT32(0, testIface->txRelay);
}

/**
 * Own-originating packet with NO active override (normal, non-retransmission
 * send).  getPacketTime() must see the base CR and no restore call is made.
 */
static void test_completeSending_noOverride_seesBaseCr()
{
    const uint8_t baseCr = 5;

    testIface->setBaseCr(baseCr);
    testIface->lastAppliedCr = baseCr;

    meshtastic_MeshPacket *p = allocPacket(0, kRemoteNodeNum);
    testIface->setActivePacket(p);
    testIface->triggerCompleteSending();

    TEST_ASSERT_EQUAL_UINT8(baseCr, testIface->crSeenByGetPacketTime);
    TEST_ASSERT_EQUAL_UINT8(baseCr, testIface->lastAppliedCr);
    TEST_ASSERT_EQUAL_UINT32(1, testIface->txGood);
    TEST_ASSERT_EQUAL_UINT32(0, testIface->txRelay);
}

/**
 * The CR ordering regression must also hold for relay (rebroadcast) packets,
 * not only for packets originated by this node.  getPacketTime() must observe
 * the override CR even though the packet's `from` belongs to a remote node.
 */
static void test_completeSending_relayedPacket_withOverride_usesOverrideCr()
{
    const uint8_t baseCr = 5;
    const uint8_t overrideCr = 8;

    testIface->setBaseCr(baseCr);
    testIface->lastAppliedCr = overrideCr;
    testIface->setActiveTxCodingRateOverrideForTest(overrideCr);

    // kRemoteNodeNum ≠ kLocalNodeNum ⟹ isFromUs() returns false (relay).
    meshtastic_MeshPacket *p = allocPacket(kRemoteNodeNum, NODENUM_BROADCAST);
    testIface->setActivePacket(p);
    testIface->triggerCompleteSending();

    TEST_ASSERT_EQUAL_UINT8(overrideCr, testIface->crSeenByGetPacketTime);
    TEST_ASSERT_EQUAL_UINT8(baseCr, testIface->lastAppliedCr);
    TEST_ASSERT_EQUAL_UINT32(1, testIface->txRelay);
}

// ---------------------------------------------------------------------------
// Section 3 — completeSending() counter semantics per send scenario
// ---------------------------------------------------------------------------

/**
 * DM originated by this node (from == 0 ≡ "phone app source", unicast to a
 * peer).  txGood must be incremented; txRelay must NOT be.
 */
static void test_completeSending_ownDm_countsTxGoodOnly()
{
    const uint8_t baseCr = 5;
    testIface->setBaseCr(baseCr);
    testIface->lastAppliedCr = baseCr;

    meshtastic_MeshPacket *p = allocPacket(0, kRemoteNodeNum);
    testIface->setActivePacket(p);
    testIface->triggerCompleteSending();

    TEST_ASSERT_EQUAL_UINT32(1, testIface->txGood);
    TEST_ASSERT_EQUAL_UINT32(0, testIface->txRelay);
}

/**
 * Broadcast originated by this node.  The to-address (BROADCAST vs unicast)
 * is not inspected by completeSending(); only the from-address matters for
 * the relay counter.
 */
static void test_completeSending_ownBroadcast_countsTxGoodOnly()
{
    const uint8_t baseCr = 5;
    testIface->setBaseCr(baseCr);
    testIface->lastAppliedCr = baseCr;

    meshtastic_MeshPacket *p = allocPacket(0, NODENUM_BROADCAST);
    testIface->setActivePacket(p);
    testIface->triggerCompleteSending();

    TEST_ASSERT_EQUAL_UINT32(1, testIface->txGood);
    TEST_ASSERT_EQUAL_UINT32(0, testIface->txRelay);
}

/**
 * DM being relayed on behalf of a remote node (from == kRemoteNodeNum ≠
 * kLocalNodeNum).  Both txGood and txRelay must be incremented.
 */
static void test_completeSending_relayedDm_countsTxGoodAndTxRelay()
{
    const uint8_t baseCr = 5;
    testIface->setBaseCr(baseCr);
    testIface->lastAppliedCr = baseCr;

    meshtastic_MeshPacket *p = allocPacket(kRemoteNodeNum, kLocalNodeNum);
    testIface->setActivePacket(p);
    testIface->triggerCompleteSending();

    TEST_ASSERT_EQUAL_UINT32(1, testIface->txGood);
    TEST_ASSERT_EQUAL_UINT32(1, testIface->txRelay);
}

/**
 * Broadcast being relayed on behalf of a remote node.  Both txGood and
 * txRelay must be incremented.
 */
static void test_completeSending_relayedBroadcast_countsTxGoodAndTxRelay()
{
    const uint8_t baseCr = 5;
    testIface->setBaseCr(baseCr);
    testIface->lastAppliedCr = baseCr;

    meshtastic_MeshPacket *p = allocPacket(kRemoteNodeNum, NODENUM_BROADCAST);
    testIface->setActivePacket(p);
    testIface->triggerCompleteSending();

    TEST_ASSERT_EQUAL_UINT32(1, testIface->txGood);
    TEST_ASSERT_EQUAL_UINT32(1, testIface->txRelay);
}

// ---------------------------------------------------------------------------
// Section 4 — null-packet guard
// ---------------------------------------------------------------------------

/**
 * When completeSending() fires with sendingPacket == nullptr (e.g. a spurious
 * transmit interrupt), it must still drain any active CR override so the radio
 * is not left stuck at the override CR.  No counters should be touched.
 */
static void test_completeSending_nullPacket_withOverride_restoresBaseCr()
{
    const uint8_t baseCr = 6;
    const uint8_t overrideCr = 8;

    testIface->setBaseCr(baseCr);
    testIface->lastAppliedCr = overrideCr;
    testIface->setActiveTxCodingRateOverrideForTest(overrideCr);

    // Do NOT call setActivePacket — sendingPacket stays nullptr.
    testIface->triggerCompleteSending();

    TEST_ASSERT_EQUAL_UINT8(baseCr, testIface->lastAppliedCr);
    TEST_ASSERT_EQUAL_UINT32(0, testIface->txGood);
    TEST_ASSERT_EQUAL_UINT32(0, testIface->txRelay);
}

// ---------------------------------------------------------------------------
// Section 5 — assumption check
// ---------------------------------------------------------------------------

/**
 * Verify the fundamental assumption the fix rests on: a higher override CR
 * must produce a longer getPacketTime() result than the base CR.  If this
 * ever changes (e.g. the encoding is inverted), the ordering fix itself
 * becomes irrelevant and someone should revisit the design.
 */
static void test_overrideCr_producesLongerAirtime_thanBaseCr()
{
    const uint8_t baseCr = 5;
    const uint8_t overrideCr = 7;

    testIface->lastAppliedCr = baseCr;
    uint32_t baseAirtime = testIface->getPacketTime(100, false);

    testIface->lastAppliedCr = overrideCr;
    uint32_t overrideAirtime = testIface->getPacketTime(100, false);

    TEST_ASSERT_GREATER_THAN_UINT32(baseAirtime, overrideAirtime);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

void setup()
{
    delay(10);
    delay(2000);

    initializeTestEnvironment();

    // Created once; setUp()/tearDown() point nodeDB at it for each test.
    mockNodeDB = new MinimalMockNodeDB();

    UNITY_BEGIN();

    // Section 1 — CR selection policy
    RUN_TEST(test_base5_progression);
    RUN_TEST(test_base6_progression);
    RUN_TEST(test_base7_or_higher_progression);
    RUN_TEST(test_second_or_later_retransmission_forces_eight);

    // Section 2 — CR ordering regression
    RUN_TEST(test_completeSending_airtimeUsesOverrideCr);
    RUN_TEST(test_completeSending_noOverride_seesBaseCr);
    RUN_TEST(test_completeSending_relayedPacket_withOverride_usesOverrideCr);

    // Section 3 — counter semantics per send scenario
    RUN_TEST(test_completeSending_ownDm_countsTxGoodOnly);
    RUN_TEST(test_completeSending_ownBroadcast_countsTxGoodOnly);
    RUN_TEST(test_completeSending_relayedDm_countsTxGoodAndTxRelay);
    RUN_TEST(test_completeSending_relayedBroadcast_countsTxGoodAndTxRelay);

    // Section 4 — null-packet guard
    RUN_TEST(test_completeSending_nullPacket_withOverride_restoresBaseCr);

    // Section 5 — assumption check
    RUN_TEST(test_overrideCr_producesLongerAirtime_thanBaseCr);

    exit(UNITY_END());
}

void loop() {}
