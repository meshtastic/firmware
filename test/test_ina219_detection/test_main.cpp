// Pins matchesIna219ConfigurationSignature() in src/detect/INA219Detection.h, used by ScanI2CTwoWire before its HM330X fallback.
// INA219 range/gain settings are writable, so a sensor configured by another driver must still be recognized.
// The previous reset-value-only check mislabeled a real INA219 configured to 0x219f as HM330X and disabled
// power telemetry. Other ADC/mode signatures must stay excluded because several unrelated devices share 0x40.
#include "MeshTypes.h"
#include "TestUtil.h"
#include "detect/INA219Detection.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_recognizes_power_on_configuration()
{
    TEST_ASSERT_TRUE(matchesIna219ConfigurationSignature(0x399f));
}

void test_recognizes_python_driver_configuration()
{
    TEST_ASSERT_TRUE(matchesIna219ConfigurationSignature(0x219f));
}

void test_accepts_both_voltage_ranges_and_all_gain_settings()
{
    const uint16_t configurations[] = {0x019f, 0x099f, 0x119f, 0x199f, 0x219f, 0x299f, 0x319f, 0x399f};
    for (uint16_t configuration : configurations)
        TEST_ASSERT_TRUE(matchesIna219ConfigurationSignature(configuration));
}

void test_rejects_other_shared_address_signatures()
{
    const uint16_t configurations[] = {0x0000, 0xffff, 0x4127, 0x6127, 0x8000, 0x799f};
    for (uint16_t configuration : configurations)
        TEST_ASSERT_FALSE(matchesIna219ConfigurationSignature(configuration));
}

void test_keeps_adc_and_mode_signature_bits_significant()
{
    TEST_ASSERT_FALSE(matchesIna219ConfigurationSignature(0x3998));
    TEST_ASSERT_FALSE(matchesIna219ConfigurationSignature(0x399b));
    TEST_ASSERT_FALSE(matchesIna219ConfigurationSignature(0x391f));
    TEST_ASSERT_FALSE(matchesIna219ConfigurationSignature(0x3997));
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_recognizes_power_on_configuration);
    RUN_TEST(test_recognizes_python_driver_configuration);
    RUN_TEST(test_accepts_both_voltage_ranges_and_all_gain_settings);
    RUN_TEST(test_rejects_other_shared_address_signatures);
    RUN_TEST(test_keeps_adc_and_mode_signature_bits_significant);
    exit(UNITY_END());
}

void loop() {}
