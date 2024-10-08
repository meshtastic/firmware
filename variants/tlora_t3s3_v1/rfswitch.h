#include "RadioLib.h"

static const uint32_t rfswitch_dio_pins[] = {RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC};

static const Module::RfSwitchMode_t rfswitch_table[] = {
    // mode                  DIO5  DIO6
    {LR11x0::MODE_STBY, {LOW, LOW}},  {LR11x0::MODE_RX, {HIGH, LOW}},
    {LR11x0::MODE_TX, {LOW, HIGH}},   {LR11x0::MODE_TX_HP, {LOW, HIGH}},
    {LR11x0::MODE_TX_HF, {LOW, LOW}}, {LR11x0::MODE_GNSS, {LOW, LOW}},
    {LR11x0::MODE_WIFI, {LOW, LOW}},  END_OF_MODE_TABLE,
};