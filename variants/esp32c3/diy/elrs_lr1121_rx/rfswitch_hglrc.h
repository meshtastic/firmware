#include "RadioLib.h"

// HGLRC "C3 900/2400 Dual Band Nano/PRO RX" RF switch mapping.
// Source: ExpressLRS LR1121Driver::SetDioAsRfSwitch() (src/lib/LR1121Driver/LR1121.cpp:299-308),
// fed from this product's radio_rfsw_ctrl=[31,0,12,4,4,10,0,9] overlay in Targets/targets.json
// (unlike the DEFAULT boards, which have none and fall back to the compiled-in default).
// ELRS: RfswEnable=0b11111 (DIO5-9), Rx=DIO7+DIO8, Tx=DIO7, TxHP=DIO7, TxHF=DIO6+DIO8, Wifi/HF-Rx=DIO5+DIO8.
//
// RadioLib has no separate "HF receive" mode — it uses MODE_RX for both bands, so 2.4GHz RX is
// routed through the sub-GHz Rx path here. Sub-GHz (the normal region) is correct; 2.4GHz RX
// may need a different table and is not handled by a static table. DIO9 is enabled by the mask
// but asserted by no mode, and RadioLib's LR11x0 RF-switch API only exposes DIO5-8, so it is dropped.

static const uint32_t rfswitch_dio_pins[] = {RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_LR11X0_DIO7,
                                             RADIOLIB_LR11X0_DIO8, RADIOLIB_NC};

static const Module::RfSwitchMode_t rfswitch_table[] = {
    // clang-format off
    // mode              DIO5  DIO6  DIO7  DIO8
    {LR11x0::MODE_STBY,  {LOW,  LOW,  LOW,  LOW}},
    {LR11x0::MODE_RX,    {LOW,  LOW,  HIGH, HIGH}},
    {LR11x0::MODE_TX,    {LOW,  LOW,  HIGH, LOW}},
    {LR11x0::MODE_TX_HP, {LOW,  LOW,  HIGH, LOW}},
    {LR11x0::MODE_TX_HF, {LOW,  HIGH, LOW,  HIGH}},
    {LR11x0::MODE_GNSS,  {LOW,  LOW,  LOW,  LOW}},
    {LR11x0::MODE_WIFI,  {HIGH, LOW,  LOW,  HIGH}},
    END_OF_MODE_TABLE,
    // clang-format on
};
