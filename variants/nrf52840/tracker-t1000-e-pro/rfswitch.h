#include "RadioLib.h"
#include "nrf.h"

static const uint32_t lr2021_rfswitch_dio_pins[] = {RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC};

static const Module::RfSwitchMode_t lr2021_rfswitch_table[] = {
    {LR2021::MODE_STBY, {}}, {LR2021::MODE_RX, {}}, {LR2021::MODE_TX, {}}, {LR2021::MODE_RX_HF, {}}, {LR2021::MODE_TX_HF, {}},
    END_OF_MODE_TABLE,
};
