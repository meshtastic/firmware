#include "RadioLib.h"

// RadioMaster XR1 / XR3 and Zerodrag Nexus Duo "900/2400 Dual Band RX" RF switch mapping.
// Source: ExpressLRS LR1121Driver::SetDioAsRfSwitch() (src/lib/LR1121Driver/LR1121.cpp:299-308),
// fed from this product's radio_rfsw_ctrl=[15,0,12,8,8,6,0,5] overlay in Targets/targets.json.
// Shared by 3 boards: RadioMaster XR1, RadioMaster XR3 Diversity, Zerodrag Nexus Duo.
// ELRS: RfswEnable=0b1111 (DIO5-8), Rx=DIO7+DIO8, Tx=DIO8, TxHP=DIO8, TxHF=DIO6+DIO7, Wifi/HF-Rx=DIO5+DIO7.
//
// RadioLib has no separate "HF receive" mode — it uses MODE_RX for both bands, so 2.4GHz RX is
// routed through the sub-GHz Rx path here. Sub-GHz (the normal region) is correct; 2.4GHz RX
// may need a different table and is not handled by a static table. (Derived from the ExpressLRS
// layout; NOT verified on hardware — I only have HGLRC boards to test.)

static const uint32_t rfswitch_dio_pins[] = {RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_LR11X0_DIO7,
                                             RADIOLIB_LR11X0_DIO8, RADIOLIB_NC};

static const Module::RfSwitchMode_t rfswitch_table[] = {
    // clang-format off
    // mode              DIO5  DIO6  DIO7  DIO8
    {LR11x0::MODE_STBY,  {LOW,  LOW,  LOW,  LOW}},
    {LR11x0::MODE_RX,    {LOW,  LOW,  HIGH, HIGH}},
    {LR11x0::MODE_TX,    {LOW,  LOW,  LOW,  HIGH}},
    {LR11x0::MODE_TX_HP, {LOW,  LOW,  LOW,  HIGH}},
    {LR11x0::MODE_TX_HF, {LOW,  HIGH, HIGH, LOW}},
    {LR11x0::MODE_GNSS,  {LOW,  LOW,  LOW,  LOW}},
    {LR11x0::MODE_WIFI,  {HIGH, LOW,  HIGH, LOW}},
    END_OF_MODE_TABLE,
    // clang-format on
};
