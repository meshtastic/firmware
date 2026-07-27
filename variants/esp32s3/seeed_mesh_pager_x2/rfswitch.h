#include "RadioLib.h"

// MeshPager X2 RF switch matrix for the Semtech LR2021 chip (Wio-LR2021 module).
// Derived from SenseCAP BSP components/lr20xx/src/ral_lr20xx_bsp.c:
//   DIO5: active during RX_LF and TX_HF
//   DIO6: active during all RF states (RX_LF, TX_LF, RX_HF, TX_HF)

static const uint32_t lr20x0_rfswitch_dio_pins[] = {
    RADIOLIB_LR2021_DIO5,
    RADIOLIB_LR2021_DIO6,
    RADIOLIB_NC,
    RADIOLIB_NC,
    RADIOLIB_NC,
};

static const Module::RfSwitchMode_t lr20x0_rfswitch_table[] = {
    // mode                DIO5   DIO6
    {LR2021::MODE_STBY,  {LOW,  LOW}},   // standby: both off
    {LR2021::MODE_RX,    {HIGH, HIGH}},  // RX sub-GHz (LF): DIO5 on, DIO6 on
    {LR2021::MODE_TX,    {LOW,  HIGH}},  // TX sub-GHz (LF): DIO5 off, DIO6 on
    {LR2021::MODE_RX_HF, {LOW,  HIGH}},  // RX 2.4 GHz (HF): DIO5 off, DIO6 on
    {LR2021::MODE_TX_HF, {HIGH, HIGH}},  // TX 2.4 GHz (HF): DIO5 on, DIO6 on
    END_OF_MODE_TABLE,
};
