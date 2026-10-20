#pragma once
#include "RadioLib.h"

#ifndef LR20x0
#define LR20x0 LR2021
#endif

#ifdef USE_LR1121
// T-Beam 1W with the LR1121 module (LilyGO t_beam_1w_lr1121.md truth table): DIO8 switches the sub-GHz PA and
// TX/RX switch to TX, DIO7 enables the 2.4 GHz PA, DIO6 the 2.4 GHz LNA. DIO5 is unused and stays low, but must be
// listed: RadioLib's LR11x0 table sets each mode's bits by position in this array, counting from DIO5.
static const uint32_t rfswitch_dio_pins[] = {RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_LR11X0_DIO7,
                                             RADIOLIB_LR11X0_DIO8, RADIOLIB_NC};

static const Module::RfSwitchMode_t rfswitch_table[] = {
    // clang-format off
    // mode              DIO5  DIO6  DIO7  DIO8
    {LR11x0::MODE_STBY,  {LOW,  LOW,  LOW,  LOW}},
    {LR11x0::MODE_TX,    {LOW,  LOW,  LOW,  LOW}},
    {LR11x0::MODE_RX,    {LOW,  LOW,  LOW,  LOW}},
    {LR11x0::MODE_TX_HP, {LOW,  LOW,  LOW,  HIGH}}, // sub-GHz TX
    {LR11x0::MODE_TX_HF, {LOW,  LOW,  HIGH, LOW}},  // 2.4 GHz TX
    {LR11x0::MODE_GNSS,  {LOW,  LOW,  LOW,  LOW}},
    {LR11x0::MODE_WIFI,  {LOW,  HIGH, LOW,  LOW}},  // 2.4 GHz RX
    END_OF_MODE_TABLE,
    // clang-format on
};
#endif

#ifdef USE_LR2021
// T-Beam 1W with the LR2021 module (LilyGO t_beam_1w_lr2021.md): DIO8 enables the sub-GHz PA for TX,
// DIO6 the 2.4 GHz LNA for RX and DIO7 the 2.4 GHz PA for TX. DIO5 stays low.
static const uint32_t lr20x0_rfswitch_dio_pins[] = {RADIOLIB_LR2021_DIO5, RADIOLIB_LR2021_DIO6, RADIOLIB_LR2021_DIO7,
                                                    RADIOLIB_LR2021_DIO8, RADIOLIB_NC};

static const Module::RfSwitchMode_t lr20x0_rfswitch_table[] = {
    // clang-format off
    // mode              DIO5  DIO6  DIO7  DIO8
    {LR20x0::MODE_STBY,  {LOW, LOW,  LOW,  LOW}},
    {LR20x0::MODE_RX,    {LOW, LOW,  LOW,  LOW}},  // sub-GHz RX
    {LR20x0::MODE_TX,    {LOW, LOW,  LOW,  HIGH}}, // sub-GHz TX
    {LR20x0::MODE_RX_HF, {LOW, HIGH, LOW,  LOW}},  // 2.4 GHz RX
    {LR20x0::MODE_TX_HF, {LOW, LOW,  HIGH, LOW}},  // 2.4 GHz TX
    END_OF_MODE_TABLE,
    // clang-format on
};
#endif
