// Canonical RF switch macros from variant_NUCLEO_WL55JC1.h
// UM2592 S6.6.3: RF overview
static const RADIOLIB_PIN_TYPE rfswitch_pins[5] = {LORAWAN_RFSWITCH_PINS, RADIOLIB_NC, RADIOLIB_NC};

static const Module::RfSwitchMode_t rfswitch_table[5] = {{STM32WLx::MODE_IDLE, {LORAWAN_RFSWITCH_OFF_VALUES}},
                                                         {STM32WLx::MODE_RX, {LORAWAN_RFSWITCH_RX_VALUES}},
                                                         {STM32WLx::MODE_TX_LP, {LORAWAN_RFSWITCH_RFO_LP_VALUES}},
                                                         {STM32WLx::MODE_TX_HP, {LORAWAN_RFSWITCH_RFO_HP_VALUES}},
                                                         END_OF_MODE_TABLE};
