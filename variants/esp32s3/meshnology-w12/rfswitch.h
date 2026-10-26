#pragma once
#include "RadioLib.h"

#ifndef LR20x0
#define LR20x0 LR2021
#endif

// Meshnology W12 (schematic W12-MB-V0.2, July 2026): DIO9/DIO10/DIO11 drive the GC1109's CTX/CPS/CSD,
// DIO5/DIO6 the 2.4 GHz front end's TX/RX enables. GC1109 truth table: shutdown CSD=0; RX (LNA) CSD=1,
// CTX=0; TX bypass CSD=1, CTX=1, CPS=0; TX PA all three high.
static const uint32_t lr20x0_rfswitch_dio_pins[] = {RADIOLIB_LR2021_DIO5, RADIOLIB_LR2021_DIO6, RADIOLIB_LR2021_DIO9,
                                                    RADIOLIB_LR2021_DIO10, RADIOLIB_LR2021_DIO11};

static const Module::RfSwitchMode_t lr20x0_rfswitch_table[] = {
    // clang-format off
    // mode              DIO5  DIO6  DIO9  DIO10 DIO11
    {LR20x0::MODE_STBY,  {LOW,  LOW,  LOW,  LOW,  LOW}},  // GC1109 shut down
    {LR20x0::MODE_RX,    {LOW,  LOW,  LOW,  LOW,  HIGH}}, // sub-GHz RX: GC1109 LNA
    {LR20x0::MODE_TX,    {LOW,  LOW,  HIGH, HIGH, HIGH}}, // sub-GHz TX: GC1109 PA
    {LR20x0::MODE_RX_HF, {LOW,  HIGH, LOW,  LOW,  LOW}},  // 2.4 GHz RX
    {LR20x0::MODE_TX_HF, {HIGH, LOW,  LOW,  LOW,  LOW}},  // 2.4 GHz TX
    END_OF_MODE_TABLE,
    // clang-format on
};
