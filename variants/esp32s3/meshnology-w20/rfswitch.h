#pragma once

#include "RadioLib.h"

static const uint32_t lr20x0_rfswitch_dio_pins[] = {RADIOLIB_LR2021_DIO5, RADIOLIB_LR2021_DIO6, RADIOLIB_LR2021_DIO10,
                                                    RADIOLIB_LR2021_DIO11, RADIOLIB_NC};

static const Module::RfSwitchMode_t lr20x0_rfswitch_table[] = {
    // clang-format off
    // mode              DIO5  DIO6  DIO10 DIO11
    {LR2021::MODE_STBY,  {LOW,  LOW,  LOW,  LOW}},
    {LR2021::MODE_RX,    {LOW,  LOW,  LOW,  HIGH}},
    {LR2021::MODE_TX,    {LOW,  LOW,  HIGH, HIGH}},
    {LR2021::MODE_RX_HF, {LOW,  HIGH, LOW,  LOW}},
    {LR2021::MODE_TX_HF, {HIGH, LOW,  LOW,  LOW}},
    END_OF_MODE_TABLE,
    // clang-format on
};
