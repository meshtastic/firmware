// Unit tests for resolveApmChargingStatus() in src/Power.cpp - which reading wins when a board both
// detects USB through the nRF52 APM block (NRF_APM) and has a charge-status pin of its own.
//
// The APM block knows one thing: whether VBUS is present. That stays true for as long as a cable is
// attached, whatever the charger is doing, so a board reported as charging off APM alone is reported
// as charging while its cell discharges. Three variants carry both NRF_APM and a charge-status pin -
// seeed_mesh_tracker_X1, seeed_wio_tracker_L1_Pro_1W and tracker-t1000-e - and the regression guarded
// here is the original shape, where the APM block assigned isCharging and hasUSB together at the end
// of readPowerStatus(), after the pin had already been read, so the pin never reached the reported
// status at all and no log could show charging stopping.
//
// Boards with no such pin still have nothing but VBUS to go on; for them the OptUnknown input has to
// keep falling back to it.
#include "Arduino.h"
#include "PowerStatus.h"
#include "TestUtil.h"
#include <unity.h>

// Declared here rather than via Power.h, which pulls in the ADC and telemetry sensor headers.
// A signature change breaks the link rather than silently diverging from the definition.
meshtastic::OptionalBool resolveApmChargingStatus(bool apmUsbConnected, meshtastic::OptionalBool pinCharging);

using meshtastic::OptFalse;
using meshtastic::OptTrue;
using meshtastic::OptUnknown;

void setUp(void) {}
void tearDown(void) {}

// The case this function exists for: a cable is attached, the charger has stopped, and the board can
// see that it has. Reporting "charging" here is what hides a flat cell behind a plugged-in node.
void test_a_pin_that_says_not_charging_outranks_vbus(void)
{
    TEST_ASSERT_EQUAL(OptFalse, resolveApmChargingStatus(true, OptFalse));
}

void test_a_pin_that_says_charging_is_kept(void)
{
    TEST_ASSERT_EQUAL(OptTrue, resolveApmChargingStatus(true, OptTrue));
}

// A charge-status pin may assert before the APM block has noticed the rail - it takes ~20s to follow
// a change. The pin is still the better evidence about the charger.
void test_a_pin_outranks_vbus_in_both_directions(void)
{
    TEST_ASSERT_EQUAL(OptTrue, resolveApmChargingStatus(false, OptTrue));
    TEST_ASSERT_EQUAL(OptFalse, resolveApmChargingStatus(false, OptFalse));
}

// No pin on this board: VBUS is all there is, and the old behaviour has to survive unchanged.
void test_without_a_pin_vbus_decides(void)
{
    TEST_ASSERT_EQUAL(OptTrue, resolveApmChargingStatus(true, OptUnknown));
    TEST_ASSERT_EQUAL(OptFalse, resolveApmChargingStatus(false, OptUnknown));
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_a_pin_that_says_not_charging_outranks_vbus);
    RUN_TEST(test_a_pin_that_says_charging_is_kept);
    RUN_TEST(test_a_pin_outranks_vbus_in_both_directions);
    RUN_TEST(test_without_a_pin_vbus_decides);
    exit(UNITY_END());
}

void loop() {}
