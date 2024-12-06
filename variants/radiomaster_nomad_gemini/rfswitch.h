#include "RadioLib.h"

static const uint32_t rfswitch_dio_pins[] = {RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_LR11X0_DIO7,
                                             RADIOLIB_LR11X0_DIO8, RADIOLIB_NC};

static const Module::RfSwitchMode_t rfswitch_table[] = {
    // mode                  DIO5  DIO6 DIO7 DIO8
    {LR11x0::MODE_STBY, {LOW, LOW, LOW, LOW}},   {LR11x0::MODE_RX, {LOW, LOW, HIGH, LOW}},
    {LR11x0::MODE_TX, {LOW, LOW, LOW, HIGH}},    {LR11x0::MODE_TX_HP, {LOW, LOW, LOW, HIGH}},
    {LR11x0::MODE_TX_HF, {LOW, HIGH, LOW, LOW}}, {LR11x0::MODE_GNSS, {LOW, LOW, LOW, LOW}},
    {LR11x0::MODE_WIFI, {LOW, LOW, LOW, LOW}},   END_OF_MODE_TABLE,
};

/*

DIO5: RXEN 2.4GHz
DIO6: TXEN 2.4GHz
DIO7: RXEN 900MHz
DIO8: TXEN 900MHz

*/