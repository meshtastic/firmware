// Pins from https://forum.rakwireless.com/t/rak3172-internal-schematic/4557/2
// PB8, PC13

static const RADIOLIB_PIN_TYPE rfswitch_pins[5] = {PB8, PC13, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC};

static const Module::RfSwitchMode_t rfswitch_table[4] = {
    {STM32WLx::MODE_IDLE, {LOW, LOW}}, {STM32WLx::MODE_RX, {HIGH, LOW}}, {STM32WLx::MODE_TX_HP, {LOW, HIGH}}, END_OF_MODE_TABLE};