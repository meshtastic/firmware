#include "RadioLib.h"
#include "nrf.h"

static const uint32_t rfswitch_dio_pins[] = {
    RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC // RADIOLIB_LR11X0_DIO7
};

static const Module::RfSwitchMode_t rfswitch_table[] = {
    // mode                DIO5 DIO6 // DIO7
    {LR11x0::MODE_STBY, {LOW, LOW}},    // { LR11x0::MODE_STBY, { LOW, LOW, LOW } },
    {LR11x0::MODE_RX, {HIGH, LOW}},     //   { LR11x0::MODE_RX, { HIGH, LOW, LOW } },
    {LR11x0::MODE_TX, {HIGH, HIGH}},    // { LR11x0::MODE_TX, { HIGH, HIGH, LOW } },
    {LR11x0::MODE_TX_HP, {LOW, HIGH}},  // { LR11x0::MODE_TX_HP, { LOW, HIGH, HIGH } },
    {LR11x0::MODE_TX_HF, {HIGH, HIGH}}, // { LR11x0::MODE_TX_HF, { LOW, LOW, HIGH } },
    {LR11x0::MODE_GNSS, {LOW, LOW}},    // { LR11x0::MODE_GNSS, { LOW, LOW, LOW } },
    {LR11x0::MODE_WIFI, {LOW, LOW}},    // { LR11x0::MODE_WIFI, { LOW, LOW, LOW } },
    END_OF_MODE_TABLE,
};
