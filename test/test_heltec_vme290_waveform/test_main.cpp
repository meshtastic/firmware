// Pins the Heltec Vision Master E290 FAST/FULL update-sequence choice.
//
// Two e-paper flexes ship on the same E290 PCB. Until a panel choice is saved,
// FAST must promote to FULL (usable on both). After selection, CustomLut uses
// MCU LUT sequence 0xCF and OtpFast uses OTP mode-2 sequence 0xFF. FULL is
// always OTP 0xF7. This suite guards those mappings without display hardware.
#include "TestUtil.h"
#include "graphics/niche/Drivers/EInk/HeltecVME290Waveform.h"
#include <unity.h>

using namespace NicheGraphics::Drivers;

void setUp(void) {}
void tearDown(void) {}

void test_unset_promotes_fast_to_full()
{
    TEST_ASSERT_TRUE(heltecVME290PromoteFastToFull(HeltecVME290Waveform::Unset));
    TEST_ASSERT_EQUAL_UINT8(HELTEC_VME290_FULL, heltecVME290EffectiveType(HeltecVME290Waveform::Unset, HELTEC_VME290_FAST));
}

void test_selected_modes_keep_fast()
{
    TEST_ASSERT_FALSE(heltecVME290PromoteFastToFull(HeltecVME290Waveform::CustomLut));
    TEST_ASSERT_FALSE(heltecVME290PromoteFastToFull(HeltecVME290Waveform::OtpFast));
    TEST_ASSERT_EQUAL_UINT8(HELTEC_VME290_FAST, heltecVME290EffectiveType(HeltecVME290Waveform::CustomLut, HELTEC_VME290_FAST));
    TEST_ASSERT_EQUAL_UINT8(HELTEC_VME290_FAST, heltecVME290EffectiveType(HeltecVME290Waveform::OtpFast, HELTEC_VME290_FAST));
}

void test_full_always_uses_otp_sequence()
{
    TEST_ASSERT_EQUAL_UINT8(0xF7, heltecVME290UpdateSequence(HeltecVME290Waveform::Unset, HELTEC_VME290_FULL));
    TEST_ASSERT_EQUAL_UINT8(0xF7, heltecVME290UpdateSequence(HeltecVME290Waveform::CustomLut, HELTEC_VME290_FULL));
    TEST_ASSERT_EQUAL_UINT8(0xF7, heltecVME290UpdateSequence(HeltecVME290Waveform::OtpFast, HELTEC_VME290_FULL));
}

void test_custom_lut_fast_sequence()
{
    TEST_ASSERT_EQUAL_UINT8(0xCF, heltecVME290UpdateSequence(HeltecVME290Waveform::CustomLut, HELTEC_VME290_FAST));
}

void test_otp_fast_sequence()
{
    TEST_ASSERT_EQUAL_UINT8(0xFF, heltecVME290UpdateSequence(HeltecVME290Waveform::OtpFast, HELTEC_VME290_FAST));
}

// Unset + FAST is promoted before the sequence byte is chosen, so the panel
// still gets a full OTP refresh rather than a no-op OTP FAST on the old flex.
void test_unset_fast_resolves_to_full_sequence()
{
    TEST_ASSERT_EQUAL_UINT8(0xF7, heltecVME290UpdateSequence(HeltecVME290Waveform::Unset, HELTEC_VME290_FAST));
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_unset_promotes_fast_to_full);
    RUN_TEST(test_selected_modes_keep_fast);
    RUN_TEST(test_full_always_uses_otp_sequence);
    RUN_TEST(test_custom_lut_fast_sequence);
    RUN_TEST(test_otp_fast_sequence);
    RUN_TEST(test_unset_fast_resolves_to_full_sequence);
    exit(UNITY_END());
}

void loop() {}
