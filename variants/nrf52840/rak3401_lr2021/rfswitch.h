#pragma once
#include "RadioLib.h"

#ifndef LR20x0
#define LR20x0 LR2021
#endif

// RAK13700: DIO7 = LF TX/RX (HIGH=RX, LOW=TX); DIO9 = HF TX/RX (HIGH=TX, LOW=RX)
static const uint32_t lr20x0_rfswitch_dio_pins[] = {RADIOLIB_LR2021_DIO7, RADIOLIB_LR2021_DIO9, RADIOLIB_NC, RADIOLIB_NC,
                                                    RADIOLIB_NC};

static const Module::RfSwitchMode_t lr20x0_rfswitch_table[] = {
    // clang-format off
    // mode              DIO7  DIO9
    {LR20x0::MODE_STBY,  {LOW,  LOW}},
    {LR20x0::MODE_RX,    {HIGH, LOW}},  // LF RX
    {LR20x0::MODE_TX,    {LOW,  LOW}},  // LF TX
    {LR20x0::MODE_RX_HF, {LOW,  LOW}},  // HF RX
    {LR20x0::MODE_TX_HF, {LOW,  HIGH}}, // HF TX
    END_OF_MODE_TABLE,
    // clang-format on
};
