#pragma once
#include "RadioLib.h"

// Keep LR20x0 naming while RadioLib exposes LR2021 symbols.
#ifndef LR20x0
#define LR20x0 LR2021
#endif

// LR1121 modules differ in how their internal RF switch is wired, so the matrix must match the
// module actually fitted. The Ebyte E80-900M2213S is the default; define one of the alternatives
// below in your build_flags (or above the #include of this header) to select a different module.
//
//   LR1121_MODULE_E80  Ebyte E80-900M2213S  (default if nothing is defined)
//   LR1121_MODULE_WIO  Seeed Wio-LR1121
#ifdef USE_LR1121

#if !defined(LR1121_MODULE_E80) && !defined(LR1121_MODULE_WIO)
#define LR1121_MODULE_E80
#endif
#if defined(LR1121_MODULE_E80) && defined(LR1121_MODULE_WIO)
#error "Define only one LR1121_MODULE_* option"
#endif

#ifdef LR1121_MODULE_E80
// This is rewritten to match the requirements of the E80-900M2213S
// The E80 does not conform to the reference Semtech switches(!) and therefore needs a custom matrix.
// See footnote #3 in "https://www.cdebyte.com/products/E80-900M2213S/2#Pin"
// RF Switch Matrix SubG RFO_HP_LF / RFO_LP_LF / RFI_[NP]_LF0
// DIO5 -> RFSW0_V1
// DIO6 -> RFSW1_V2
// DIO7 -> not connected on E80 module - note that GNSS and Wifi scanning are not possible.
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
#endif // LR1121_MODULE_E80

#ifdef LR1121_MODULE_WIO
// Seeed Wio-LR1121. Unlike the E80 this module DOES follow the Semtech reference topology
// (SWSD006: rx = RFSW0, tx = RFSW0|RFSW1, tx_hp = RFSW1).
// The internal switch is a Skyworks SKY13373-460LF driven by two lines only:
// DIO5 -> V1
// DIO6 -> V2
// Datasheet section 4.5, "True Table of the Internal RF Switch":
//   V1 V2  Status
//    0  0  Shutdown
//    1  0  RFI_P_LF & RFI_N_LF   (receive)
//    0  1  RFO_HP_LF             (sub-GHz high power)
//    1  1  RFO_LP_LF             (sub-GHz low power)
// The LR1121 has no GNSS or WiFi scanning, so those modes are left in the shutdown state.
static const uint32_t rfswitch_dio_pins[] = {RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC};

static const Module::RfSwitchMode_t rfswitch_table[] = {
    // clang-format off
    // mode              DIO5  DIO6
    {LR11x0::MODE_STBY,  {LOW,  LOW}},
    {LR11x0::MODE_RX,    {HIGH, LOW}},
    {LR11x0::MODE_TX,    {HIGH, HIGH}},
    {LR11x0::MODE_TX_HP, {LOW,  HIGH}},
    {LR11x0::MODE_TX_HF, {LOW,  LOW}},
    {LR11x0::MODE_GNSS,  {LOW,  LOW}},
    {LR11x0::MODE_WIFI,  {LOW,  LOW}},
    END_OF_MODE_TABLE,
    // clang-format on
};
#endif // LR1121_MODULE_WIO

#endif // USE_LR1121

// LR2021 RF switch matrix following the standard Semtech / Seeed T1000-E reference topology.
// DIO5 -> antenna path select (HIGH = sub-GHz LF)
// DIO6 -> TX enable / HP PA select
// DIO7 -> not connected (no GNSS on LR2021)
// DIO8 -> RF front-end power enable
#ifdef USE_LR2021
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
#endif
