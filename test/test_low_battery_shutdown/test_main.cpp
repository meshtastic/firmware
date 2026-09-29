// Unit tests for updateLowVoltageCounter() in src/Power.cpp - the gate on the low-battery deep sleep.
//
// Power::readPowerStatus() calls this once per Power thread cycle (20s) with the freshly probed
// battery state. When it returns true the device takes EVENT_LOW_BATTERY -> stateLowBattSDS ->
// doDeepSleep(config.power.sds_secs), and sds_secs defaults to UINT32_MAX, so a false positive parks
// a node for the ~24.8-day clamp with only RST or a power cycle to recover it. That asymmetry is why
// the counter has to be conservative: a missed shutdown costs a flat battery, a spurious one costs
// the whole node.
//
// The contract is that only *consecutive* confirmed-low readings count. The regression guarded is
// the original shape, where the reset lived inside the "battery present and not on USB" guard rather
// than beside it. A board with no battery reads a floating divider that drifts across the 2600mV
// battery-present threshold, so each excursion into the window bumped the counter and nothing outside
// the window ever cleared it; eleven such flickers, spread over any span at all, deep-slept a healthy
// USB-powered node. Reported for Heltec V4 on USB with no battery in meshtastic/firmware#11796.
//
// Also pins the cutoff as a pack voltage. readPowerStatus() passes OCV[NUM_OCV_POINTS-1] * NUM_CELLS;
// it previously passed the bare single-cell OCV point, which no multi-cell pack can fall below, so
// those boards would have discharged to destruction instead of shutting down.
#include "Arduino.h"
#include "TestUtil.h"
#include <cstdint>
#include <unity.h>

// Declared here rather than via Power.h, which pulls in the ADC and telemetry sensor headers.
// A signature change breaks the link rather than silently diverging from the definition.
bool updateLowVoltageCounter(uint8_t &counter, bool hasBattery, bool hasUsb, uint16_t battMv, uint16_t cutoffMv);

// LOW_VOLTAGE_READINGS_BEFORE_SHUTDOWN, spelled out so a change to it has to be a deliberate edit here.
static constexpr uint8_t kReadingsBeforeShutdown = 10;

// The default LiIon curve's lowest OCV point, and a single-cell pack comfortably below it.
static constexpr uint16_t kCutoffMv = 3100;
static constexpr uint16_t kLowMv = 2900;
static constexpr uint16_t kHealthyMv = 3900;

// One reading of a battery-backed node running off its battery - the only case that may ever count.
static bool lowReading(uint8_t &counter, uint16_t battMv = kLowMv, uint16_t cutoffMv = kCutoffMv)
{
    return updateLowVoltageCounter(counter, true, false, battMv, cutoffMv);
}

void setUp(void) {}
void tearDown(void) {}

void test_an_unbroken_run_of_low_readings_shuts_down(void)
{
    uint8_t counter = 0;

    for (uint8_t i = 0; i < kReadingsBeforeShutdown; i++)
        TEST_ASSERT_FALSE_MESSAGE(lowReading(counter), "must not fire before the full run is seen");

    TEST_ASSERT_TRUE(lowReading(counter));
}

void test_a_healthy_reading_clears_the_run(void)
{
    uint8_t counter = 0;
    for (uint8_t i = 0; i < kReadingsBeforeShutdown; i++)
        lowReading(counter);

    TEST_ASSERT_FALSE(lowReading(counter, kHealthyMv));
    TEST_ASSERT_EQUAL_UINT8(0, counter);
    TEST_ASSERT_FALSE_MESSAGE(lowReading(counter), "the run restarts from zero, it does not resume");
}

// #11796: the battery-less board. Its floating divider reads "no battery" as often as it reads a
// phantom one, and it is never on a detectable USB rail, so the gaps are the only thing that can
// save it. Interleaving them must hold the counter at zero however long this runs.
void test_no_battery_reading_clears_the_run(void)
{
    uint8_t counter = 0;

    for (int cycle = 0; cycle < 50; cycle++) {
        TEST_ASSERT_FALSE(lowReading(counter));
        TEST_ASSERT_FALSE(updateLowVoltageCounter(counter, false, false, kLowMv, kCutoffMv));
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, counter, "a reading with no battery resets, it does not skip");
    }
}

void test_usb_power_clears_the_run(void)
{
    uint8_t counter = 0;
    for (uint8_t i = 0; i < kReadingsBeforeShutdown; i++)
        lowReading(counter);

    TEST_ASSERT_FALSE(updateLowVoltageCounter(counter, true, true, kLowMv, kCutoffMv));
    TEST_ASSERT_EQUAL_UINT8(0, counter);
}

// A pack sitting exactly on the cutoff is not below it; the OCV table's last point is a valid voltage.
void test_the_cutoff_is_exclusive(void)
{
    uint8_t counter = 0;
    TEST_ASSERT_FALSE(lowReading(counter, kCutoffMv));
    TEST_ASSERT_EQUAL_UINT8(0, counter);

    TEST_ASSERT_FALSE(lowReading(counter, kCutoffMv - 1));
    TEST_ASSERT_EQUAL_UINT8(1, counter);
}

// The caller scales by NUM_CELLS. Against the bare single-cell point a 2S pack never reads low at all.
void test_the_cutoff_is_a_pack_voltage(void)
{
    constexpr uint16_t twoCellCutoffMv = kCutoffMv * 2;
    constexpr uint16_t flatTwoCellPackMv = 6000;

    uint8_t counter = 0;
    for (uint8_t i = 0; i <= kReadingsBeforeShutdown; i++)
        TEST_ASSERT_EQUAL(i == kReadingsBeforeShutdown, lowReading(counter, flatTwoCellPackMv, twoCellCutoffMv));

    counter = 0;
    for (uint8_t i = 0; i <= kReadingsBeforeShutdown; i++)
        TEST_ASSERT_FALSE_MESSAGE(lowReading(counter, flatTwoCellPackMv, kCutoffMv),
                                  "unscaled cutoff: the regression that never shuts a 2S pack down");
}

// The counter is a uint8_t and the caller keeps calling after it fires, so it must saturate. Were it
// to wrap, the node would come back up, count to 255 again and re-sleep in an unattended loop.
void test_the_counter_saturates_rather_than_wrapping(void)
{
    uint8_t counter = 0;

    for (int i = 0; i < 400; i++) {
        const bool shutdown = lowReading(counter);
        if (i >= kReadingsBeforeShutdown)
            TEST_ASSERT_TRUE_MESSAGE(shutdown, "once tripped it stays tripped until a reading clears it");
    }
    TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, counter);
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_an_unbroken_run_of_low_readings_shuts_down);
    RUN_TEST(test_a_healthy_reading_clears_the_run);
    RUN_TEST(test_no_battery_reading_clears_the_run);
    RUN_TEST(test_usb_power_clears_the_run);
    RUN_TEST(test_the_cutoff_is_exclusive);
    RUN_TEST(test_the_cutoff_is_a_pack_voltage);
    RUN_TEST(test_the_counter_saturates_rather_than_wrapping);
    exit(UNITY_END());
}

void loop() {}
