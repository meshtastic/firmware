#include "RadioLib.h"

static const uint32_t rfswitch_dio_pins[] = {RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_LR11X0_DIO7,
                                             RADIOLIB_LR11X0_DIO8, RADIOLIB_NC};

static const Module::RfSwitchMode_t rfswitch_table[] = {
    // mode                  DIO5  DIO6 DIO7 DIO8
    {LR11x0::MODE_STBY, {LOW, LOW, LOW, LOW}},   {LR11x0::MODE_RX, {LOW, LOW, HIGH, LOW}},
    {LR11x0::MODE_TX, {LOW, LOW, LOW, HIGH}},    {LR11x0::MODE_TX_HP, {LOW, LOW, LOW, HIGH}},
    {LR11x0::MODE_TX_HF, {LOW, HIGH, LOW, LOW}}, {LR11x0::MODE_GNSS, {LOW, LOW, LOW, LOW}},
    {LR11x0::MODE_WIFI, {HIGH, LOW, LOW, LOW}},  END_OF_MODE_TABLE,
};

/*

DIO5: RXEN 2.4GHz
DIO6: TXEN 2.4GHz
DIO7: RXEN 900MHz
DIO8: TXEN 900MHz


  "radio_dcdc": true,
  "radio_rfo_hf": true,

    "power_apc2": 26,
    "power_min": 0,
    "power_high": 6,
    "power_max": 6,
    "power_default": 3,
    "power_control": 3, POWER_OUTPUT_DACWRITE // use internal dacWrite function to set value on GPIO_PIN_RFamp_APC2
                         [0,   1,   2,   3,   4,   5,   6 ] // 0-6
    "power_values":      [120, 120, 120, 120, 120, 120, 95] // DAC Value
    "power_values2":     [-17, -16, -14, -11, -7,  -3,  5 ] // 900M
    "power_values_dual": [-18, -14,  -8,  -6, -2,   3,  5 ] // 2.4G

    // default value 0 means direct!
#define POWER_OUTPUT_DACWRITE (hardware_int(HARDWARE_power_control)==3)
#define POWER_OUTPUT_VALUES hardware_i16_array(HARDWARE_power_values)
#define POWER_OUTPUT_VALUES_COUNT hardware_int(HARDWARE_power_values_count)
#define POWER_OUTPUT_VALUES2 hardware_i16_array(HARDWARE_power_values2)
#define POWER_OUTPUT_VALUES_DUAL hardware_i16_array(HARDWARE_power_values_dual)
#define POWER_OUTPUT_VALUES_DUAL_COUNT hardware_int(HARDWARE_power_values_dual_count)

#define GPIO_PIN_FAN_EN hardware_pin(HARDWARE_misc_fan_en)

    case PWR_10mW: return 10;
    case PWR_25mW: return 14;
    case PWR_50mW: return 17;
    case PWR_100mW: return 20;
    case PWR_250mW: return 24;
    case PWR_500mW: return 27;
    case PWR_1000mW: return 30;

    95 -> +25dBm
    120 -> +24dBm
*/