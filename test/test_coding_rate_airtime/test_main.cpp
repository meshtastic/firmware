/**
 * Regression tests for the airtime calculation ordering in
 * RadioLibInterface::completeSending().
 *
 * The bug: completeSending() used to call restoreTemporaryCodingRateOverride()
 * BEFORE getPacketTime().  Because the TX path for retransmissions with an
 * escalated CR calls applyCodingRate(overrideCr) on the hardware, and
 * getPacketTime() reads that hardware state, the early restore left the radio
 * at the base CR when getPacketTime() ran — producing an airtime that was too
 * short.
 *
 * The fix: getPacketTime(p) is now called first, while the hardware is still
 * at the override CR, and restoreTemporaryCodingRateOverride() runs afterwards.
 *
 * This suite exercises completeSending() through a minimal test double and
 * verifies the CR seen by getPacketTime() equals the override CR, not the
 * base CR.
 */

#include "MeshTypes.h"
#include "NodeDB.h"
#include "TestRadioLibInterface.h" // test double + wrappers
#include "TestUtil.h"
#include "airtime.h"
#include <unity.h>

// ---------------------------------------------------------------------------
// Test globals
// ---------------------------------------------------------------------------

static TestRadioLibInterface *testIface = nullptr;
static AirTime *savedAirTime = nullptr;
static AirTime *testAirTime = nullptr;

// ---------------------------------------------------------------------------
// Unity lifecycle
// ---------------------------------------------------------------------------

void setUp(void)
{
    // AirTime::logAirtime() must not dereference a null pointer.
    // Construct a real AirTime; its logAirtime() simply adds to internal
    // counters which is harmless for our test.
    savedAirTime = airTime;
    testAirTime = new AirTime();
    airTime = testAirTime;

    // isFromUs(p) with p->from == 0 short-circuits before touching nodeDB
    // (the LHS of the || is true), so no nodeDB setup is required.

    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    initRegion();

    testIface = new TestRadioLibInterface();
}

void tearDown(void)
{
    delete testIface;
    testIface = nullptr;

    airTime = savedAirTime;
    delete testAirTime;
    testAirTime = nullptr;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

/**
 * When completeSending() fires with an active CR override (simulating the
 * second or later retransmission of a packet), getPacketTime() must see the
 * OVERRIDE CR — not the base CR that restoreTemporaryCodingRateOverride()
 * restores afterward.
 *
 * Regression: the old code called restoreTemporaryCodingRateOverride() first,
 * so getPacketTime() always saw the base CR.
 */
static void test_completeSending_airtimeUsesOverrideCr()
{
    const uint8_t baseCr = 5;
    const uint8_t overrideCr = 7;

    // Set the base coding rate on the RadioInterface (as applyModemConfig would).
    testIface->setBaseCr(baseCr);

    // Simulate the hardware state AFTER applyTemporaryCodingRateOverride() ran:
    // the radio was put into the override CR.
    testIface->lastAppliedCr = overrideCr;

    // Simulate an active override (set by applyTemporaryCodingRateOverride).
    testIface->setActiveTxCodingRateOverrideForTest(overrideCr);

    // Allocate a packet from the real pool so packetPool.release() inside
    // completeSending() doesn't corrupt memory.  Use p->from == 0 so that
    // isFromUs() short-circuits to true without touching nodeDB.
    meshtastic_MeshPacket *p = packetPool.allocZeroed();
    p->from = 0;
    p->which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    p->encrypted.size = 10;

    testIface->setActivePacket(p);

    testIface->triggerCompleteSending();

    // The CR recorded by getPacketTime() must be the override CR, proving that
    // getPacketTime() ran before restoreTemporaryCodingRateOverride() restored
    // the hardware to baseCr.
    TEST_ASSERT_EQUAL_UINT8(overrideCr, testIface->crSeenByGetPacketTime);

    // After completeSending(), the override must be cleared and the radio
    // restored to the base CR.
    TEST_ASSERT_EQUAL_UINT8(baseCr, testIface->lastAppliedCr);
}

/**
 * When there is NO active override (normal, non-retransmission send),
 * completeSending() must still work correctly: getPacketTime() sees the base
 * CR and no restore call is made.
 */
static void test_completeSending_noOverride_seesBaseCr()
{
    const uint8_t baseCr = 5;

    testIface->setBaseCr(baseCr);
    testIface->lastAppliedCr = baseCr;
    // Leave activeTxCodingRateOverride unset (nullopt).

    meshtastic_MeshPacket *p = packetPool.allocZeroed();
    p->from = 0;
    p->which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    p->encrypted.size = 10;

    testIface->setActivePacket(p);

    testIface->triggerCompleteSending();

    TEST_ASSERT_EQUAL_UINT8(baseCr, testIface->crSeenByGetPacketTime);
    // No override was active, so lastAppliedCr stays at baseCr (restore
    // is a no-op and no applyCodingRate call is made).
    TEST_ASSERT_EQUAL_UINT8(baseCr, testIface->lastAppliedCr);
}

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

    UNITY_BEGIN();
    RUN_TEST(test_completeSending_airtimeUsesOverrideCr);
    RUN_TEST(test_completeSending_noOverride_seesBaseCr);
    RUN_TEST(test_overrideCr_producesLongerAirtime_thanBaseCr);
    exit(UNITY_END());
}

void loop() {}
