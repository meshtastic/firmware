#include "RadioLib.h"

// RF Switch Matrix SubG RFO_HP_LF / RFO_LP_LF / RFI_[NP]_LF0
// DIO5 -> RFSW0_V1
// DIO6 -> RFSW1_V2
// DIO7 -> ANT_CTRL_ON + ESP_IO9/LR_GPS_ANT_DC_EN -> RFI_GPS (Bias-T GPS) (LR11x0 only)

static const uint32_t rfswitch_dio_pins[] = {RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_LR11X0_DIO7, RADIOLIB_NC,
                                             RADIOLIB_NC};

static const Module::RfSwitchMode_t rfswitch_table[] = {
    // mode                  DIO5  DIO6  DIO7
    {LR11x0::MODE_STBY, {LOW, LOW, LOW}},  {LR11x0::MODE_RX, {HIGH, LOW, LOW}},
    {LR11x0::MODE_TX, {LOW, HIGH, LOW}},   {LR11x0::MODE_TX_HP, {LOW, HIGH, LOW}},
    {LR11x0::MODE_TX_HF, {LOW, LOW, LOW}}, {LR11x0::MODE_GNSS, {LOW, LOW, HIGH}},
    {LR11x0::MODE_WIFI, {LOW, LOW, LOW}},  END_OF_MODE_TABLE,
};