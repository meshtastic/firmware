#include "RadioLib.h"

// Decoded from ExpressLRS LR1121Driver::SetDioAsRfSwitch() and each product's radio_rfsw_ctrl in
// targets.json. RadioLib has a single MODE_RX for both bands, so these tables route RX through the
// sub-GHz path; 2.4 GHz RX would need the HF-RX DIOs and isn't covered.

static const uint32_t rfswitch_dio_pins[] = {RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_LR11X0_DIO7,
                                             RADIOLIB_LR11X0_DIO8, RADIOLIB_NC};

static const Module::RfSwitchMode_t rfswitch_table[] = {
    // clang-format off
    // mode              DIO5  DIO6  DIO7  DIO8
    {LR11x0::MODE_STBY,  {LOW,  LOW,  LOW,  LOW}},
#if defined(ELRS_RFSW_HGLRC)
    // radio_rfsw_ctrl [31,0,12,4,4,10,0,9]; DIO9 is in the mask but unused by any mode
    {LR11x0::MODE_RX,    {LOW,  LOW,  HIGH, HIGH}},
    {LR11x0::MODE_TX,    {LOW,  LOW,  HIGH, LOW}},
    {LR11x0::MODE_TX_HP, {LOW,  LOW,  HIGH, LOW}},
    {LR11x0::MODE_TX_HF, {LOW,  HIGH, LOW,  HIGH}},
    {LR11x0::MODE_WIFI,  {HIGH, LOW,  LOW,  HIGH}},
#elif defined(ELRS_RFSW_RADIOMASTER)
    // radio_rfsw_ctrl [15,0,12,8,8,6,0,5]
    {LR11x0::MODE_RX,    {LOW,  LOW,  HIGH, HIGH}},
    {LR11x0::MODE_TX,    {LOW,  LOW,  LOW,  HIGH}},
    {LR11x0::MODE_TX_HP, {LOW,  LOW,  LOW,  HIGH}},
    {LR11x0::MODE_TX_HF, {LOW,  HIGH, HIGH, LOW}},
    {LR11x0::MODE_WIFI,  {HIGH, LOW,  HIGH, LOW}},
#else
    // ExpressLRS compiled-in default (no radio_rfsw_ctrl)
    {LR11x0::MODE_RX,    {LOW,  LOW,  HIGH, LOW}},
    {LR11x0::MODE_TX,    {LOW,  LOW,  LOW,  HIGH}},
    {LR11x0::MODE_TX_HP, {LOW,  LOW,  LOW,  HIGH}},
    {LR11x0::MODE_TX_HF, {LOW,  HIGH, LOW,  LOW}},
    {LR11x0::MODE_WIFI,  {HIGH, LOW,  LOW,  LOW}},
#endif
    {LR11x0::MODE_GNSS,  {LOW,  LOW,  LOW,  LOW}},
    END_OF_MODE_TABLE,
    // clang-format on
};
