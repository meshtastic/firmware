#pragma once
#include "RadioLib.h"

// Keep LR20x0 naming while RadioLib exposes LR2021 symbols.
#ifndef LR20x0
#define LR20x0 LR2021
#endif

#ifndef RFSWITCH_NRF52TCXO_LR11X0_DEFINED
#define RFSWITCH_NRF52TCXO_LR11X0_DEFINED
// This is rewritten to match the requirements of the E80-900M2213S
// The E80 does not conform to the reference Semtech switches(!) and therefore needs a custom matrix.
// See footnote #3 in "https://www.cdebyte.com/products/E80-900M2213S/2#Pin"
// RF Switch Matrix SubG RFO_HP_LF / RFO_LP_LF / RFI_[NP]_LF0
// DIO5 -> RFSW0_V1
// DIO6 -> RFSW1_V2
// DIO7 -> not connected on E80 module - note that GNSS and Wifi scanning are not possible.
#ifdef USE_LR1121
static const uint32_t rfswitch_dio_pins[] = {RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_LR11X0_DIO7, RADIOLIB_NC,
                                             RADIOLIB_NC};

static const Module::RfSwitchMode_t rfswitch_table[] = {
    // clang-format off
    // mode              DIO5  DIO6  DIO7
    {LR11x0::MODE_STBY,  {LOW,  LOW,  LOW}},
    {LR11x0::MODE_RX,    {LOW,  HIGH, LOW}},
    {LR11x0::MODE_TX,    {HIGH, HIGH, LOW}},
    {LR11x0::MODE_TX_HP, {HIGH, LOW,  LOW}},
    {LR11x0::MODE_TX_HF, {LOW,  LOW,  LOW}},
    {LR11x0::MODE_GNSS,  {LOW,  LOW,  HIGH}},
    {LR11x0::MODE_WIFI,  {LOW,  LOW,  LOW}},
    END_OF_MODE_TABLE,
    // clang-format on
};
#endif // USE_LR1121
#endif // RFSWITCH_NRF52TCXO_LR11X0_DEFINED

// LR2021 RF switch matrix following the standard Semtech / Seeed T1000-E reference topology.
// DIO5 -> antenna path select (HIGH = sub-GHz LF)
// DIO6 -> TX enable / HP PA select
// DIO7 -> not connected (no GNSS on LR2021)
// DIO8 -> RF front-end power enable
//
// LR20X0_RFSWITCH_NATIVE tells LR20x0Interface.cpp that lr20x0_rfswitch_* are defined here
// directly, so it should not alias rfswitch_dio_pins (which points to the LR11x0 table above).
#ifndef RFSWITCH_NRF52TCXO_LR20X0_DEFINED
#define RFSWITCH_NRF52TCXO_LR20X0_DEFINED
#ifdef USE_LR2021
#define LR20X0_RFSWITCH_NATIVE
// DIO7 is in the pin array for RF switch matrix completeness (the T1000-E reference
// topology includes it in the control bus), but all mode entries drive it LOW - it is
// not physically connected on this variant. Using RADIOLIB_NC here would skip the pin
// in setRfSwitchTable(), which is fine functionally, but keeping it mapped ensures the
// pin array indices align with the mode table column layout.
static const uint32_t lr20x0_rfswitch_dio_pins[] = {RADIOLIB_LR2021_DIO5, RADIOLIB_LR2021_DIO6, RADIOLIB_LR2021_DIO7,
                                                    RADIOLIB_LR2021_DIO8, RADIOLIB_NC};

static const Module::RfSwitchMode_t lr20x0_rfswitch_table[] = {
    // clang-format off
    // mode               DIO5  DIO6  DIO7  DIO8
    {LR20x0::MODE_STBY,   {LOW,  LOW,  LOW,  LOW}},
    {LR20x0::MODE_RX,     {HIGH, LOW,  LOW,  HIGH}},
    {LR20x0::MODE_TX,     {HIGH, HIGH, LOW,  HIGH}},
    {LR20x0::MODE_RX_HF,  {LOW,  LOW,  LOW,  LOW}},
    {LR20x0::MODE_TX_HF,  {LOW,  LOW,  LOW,  LOW}},
    END_OF_MODE_TABLE,
    // clang-format on
};
#endif // USE_LR2021
#endif // RFSWITCH_NRF52TCXO_LR20X0_DEFINED
