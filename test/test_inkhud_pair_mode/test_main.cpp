// Pair Mode toggle for the InkHUD Bluetooth menu must change bluetooth.mode,
// not bluetooth.fixed_pin, and its label must reflect the mode.
#include "Arduino.h"
#include "TestUtil.h"
#include <unity.h>

#include "graphics/niche/InkHUD/Applets/System/Menu/BluetoothPairMode.h"

void setUp(void) {}
void tearDown(void) {}

void test_toggle_random_to_fixed(void)
{
    TEST_ASSERT_EQUAL(meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN,
                      nextBluetoothPairMode(meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN));
}

void test_toggle_fixed_to_random(void)
{
    TEST_ASSERT_EQUAL(meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN,
                      nextBluetoothPairMode(meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN));
}

void test_toggle_leaves_no_pin_unchanged(void)
{
    TEST_ASSERT_EQUAL(meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN,
                      nextBluetoothPairMode(meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN));
}

void test_label_follows_mode(void)
{
    TEST_ASSERT_EQUAL_STRING("Pair Mode: Random", bluetoothPairModeLabel(meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN));
    TEST_ASSERT_EQUAL_STRING("Pair Mode: Fixed", bluetoothPairModeLabel(meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN));
    TEST_ASSERT_EQUAL_STRING("Pair Mode: No PIN", bluetoothPairModeLabel(meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN));
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_toggle_random_to_fixed);
    RUN_TEST(test_toggle_fixed_to_random);
    RUN_TEST(test_toggle_leaves_no_pin_unchanged);
    RUN_TEST(test_label_follows_mode);
    exit(UNITY_END());
}
