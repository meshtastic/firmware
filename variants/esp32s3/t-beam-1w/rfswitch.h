#pragma once
#include "RadioLib.h"

#ifndef LR20x0
#define LR20x0 LR2021
#endif

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
