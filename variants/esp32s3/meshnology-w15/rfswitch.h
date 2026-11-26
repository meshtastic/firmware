#pragma once

#include "RadioLib.h"

static const uint32_t lr20x0_rfswitch_dio_pins[] = {RADIOLIB_LR2021_DIO8, RADIOLIB_LR2021_DIO9, RADIOLIB_LR2021_DIO10,
                                                    RADIOLIB_LR2021_DIO11, RADIOLIB_NC};

static const Module::RfSwitchMode_t lr20x0_rfswitch_table[] = {
    // clang-format off
    // mode              DIO8  DIO9  DIO10 DIO11
    {LR2021::MODE_STBY,  {LOW,  LOW,  LOW,  LOW}},
    {LR2021::MODE_RX,    {LOW,  HIGH, LOW,  LOW}},
    {LR2021::MODE_TX,    {HIGH, LOW,  LOW,  LOW}},
    {LR2021::MODE_RX_HF, {LOW,  LOW,  HIGH, LOW}},
    {LR2021::MODE_TX_HF, {LOW,  LOW,  LOW,  HIGH}},
    END_OF_MODE_TABLE,
    // clang-format on
};
