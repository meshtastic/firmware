// Tests for the register-to-units conversion in src/modules/Telemetry/Sensor/INAFamily.h.
//
// The unified INA driver replaces five vendor libraries, so these pin the datasheet LSBs,
// register shifts and sign extension for every chip before any Wire traffic is involved.
// They guard the two mistakes easiest to make in the table: using one shift for both registers
// (INA219 needs bus >>3 and shunt >>0) and sign-extending the wrong width (INA228 is 20-bit).

#include "Arduino.h"
#include "TestUtil.h"
#include "modules/Telemetry/Sensor/INAFamily.h"
#include <cmath>
#include <cstdlib>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

static void assertNear(float expected, float actual, float tolerance = 0.01f)
{
    TEST_ASSERT_FLOAT_WITHIN(tolerance, expected, actual);
}

// --- INA219: bus >>3 at 4 mV, shunt >>0 at 10 uV, 0.1 ohm ---

void test_ina219_positive_current()
{
    InaReading r = inaConvert(INA219_CHIP, 3000u << 3, 10000, 0.1f);
    assertNear(12.0f, r.busV);
    assertNear(1000.0f, r.currentMa);
}

void test_ina219_negative_current()
{
    InaReading r = inaConvert(INA219_CHIP, 3000u << 3, (uint16_t)-10000, 0.1f);
    assertNear(-1000.0f, r.currentMa);
}

// OVF (bit 0) is a math-overflow flag for the calibration-derived current/power registers.
// The unified driver derives current from the shunt register, so OVF must not disturb it.
void test_ina219_ovf_does_not_affect_shunt_current()
{
    InaReading r = inaConvert(INA219_CHIP, (3000u << 3) | 1u, 10000, 0.1f);
    assertNear(12.0f, r.busV);
    assertNear(1000.0f, r.currentMa);
}

// --- INA226: bus 1.25 mV, shunt 2.5 uV ---

void test_ina226_positive_current()
{
    InaReading r = inaConvert(INA226_CHIP, 9600, 20000, INA226_SHUNT_R);
    assertNear(12.0f, r.busV);
    assertNear(500.0f, r.currentMa);
}

void test_ina226_negative_current()
{
    InaReading r = inaConvert(INA226_CHIP, 9600, (uint16_t)-20000, INA226_SHUNT_R);
    assertNear(-500.0f, r.currentMa);
}

// --- INA260: direct current register at 1.25 mA, internal shunt ---

void test_ina260_current_register_is_direct()
{
    InaReading r = inaConvert(INA260_CHIP, 9600, 10000, 1.0f);
    assertNear(12.0f, r.busV);
    assertNear(12500.0f, r.currentMa);
}

void test_ina260_negative_current()
{
    InaReading r = inaConvert(INA260_CHIP, 9600, (uint16_t)-10000, 1.0f);
    assertNear(-12500.0f, r.currentMa);
}

// --- INA228: 24-bit registers, 20-bit two's complement after >>4 ---

void test_ina228_positive_current()
{
    InaReading r = inaConvert(INA228_CHIP, 61440u << 4, 64000u << 4, INA228_SHUNT_R);
    assertNear(12.0f, r.busV);
    assertNear(1333.3333f, r.currentMa, 0.1f);
}

void test_ina228_sign_extension_at_20_bits()
{
    InaReading pos = inaConvert(INA228_CHIP, 0, 0x7FFFFu << 4, INA228_SHUNT_R);
    InaReading neg = inaConvert(INA228_CHIP, 0, 0x80000u << 4, INA228_SHUNT_R);

    TEST_ASSERT_TRUE(pos.currentMa > 0);
    TEST_ASSERT_TRUE(neg.currentMa < 0);
    assertNear(-pos.currentMa, neg.currentMa, 0.1f);
}

// --- INA3221: three channels, bus >>3 at 8 mV, shunt >>3 at 40 uV ---

void test_ina3221_all_three_channels()
{
    InaReading ch1 = inaConvert(INA3221_CHIP, 1500u << 3, 2500u << 3, INA3221_SHUNT_R_CH1);
    InaReading ch2 = inaConvert(INA3221_CHIP, 625u << 3, 1250u << 3, INA3221_SHUNT_R_CH2);
    InaReading ch3 = inaConvert(INA3221_CHIP, 400u << 3, (uint16_t)(0x1D8Fu << 3), INA3221_SHUNT_R_CH3);

    assertNear(12.0f, ch1.busV);
    assertNear(1000.0f, ch1.currentMa);

    assertNear(5.0f, ch2.busV);
    assertNear(500.0f, ch2.currentMa);

    assertNear(3.2f, ch3.busV);
    assertNear(-250.0f, ch3.currentMa);
}

// --- variant multiplier ---

void test_ina219_multiplier_applies_to_environment_current()
{
    TEST_ASSERT_EQUAL_FLOAT(INA219_MULTIPLIER, INA219_CHIP.currentMultiplier);

    InaChip scaled = INA219_CHIP;
    scaled.currentMultiplier = 10.0f;
    assertNear(10000.0f, inaScaleCurrent(scaled, 1000.0f));
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_ina219_positive_current);
    RUN_TEST(test_ina219_negative_current);
    RUN_TEST(test_ina219_ovf_does_not_affect_shunt_current);
    RUN_TEST(test_ina226_positive_current);
    RUN_TEST(test_ina226_negative_current);
    RUN_TEST(test_ina260_current_register_is_direct);
    RUN_TEST(test_ina260_negative_current);
    RUN_TEST(test_ina228_positive_current);
    RUN_TEST(test_ina228_sign_extension_at_20_bits);
    RUN_TEST(test_ina3221_all_three_channels);
    RUN_TEST(test_ina219_multiplier_applies_to_environment_current);
    exit(UNITY_END());
}

void loop() {}
