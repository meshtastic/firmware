// Pins from https://github.com/Xinyuan-LilyGO/T3-STM32/blob/master/hardware/T3_STM32%20V1.0%2024-07-30.pdf
// RF switch: AS169-73LF SPDT, pin V1 always high, pin V2 controlled by GPIO PB2
// Rx = PB2 high, Tx = PB2 low: https://github.com/Xinyuan-LilyGO/T3-STM32/blob/master/examples/6_SubGHz_TXRX/User/user.h

static const RADIOLIB_PIN_TYPE rfswitch_pins[5] = {PB2, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC};

static const Module::RfSwitchMode_t rfswitch_table[4] = {
    {STM32WLx::MODE_IDLE, {HIGH}}, {STM32WLx::MODE_RX, {HIGH}}, {STM32WLx::MODE_TX_HP, {LOW}}, END_OF_MODE_TABLE};
