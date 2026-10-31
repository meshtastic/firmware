/* https://wiki.seeedstudio.com/LoRa-E5_STM32WLE5JC_Module/
 * Wio-E5 module ONLY transmits through RFO_HP
 * Receive: PA4=1, PA5=0
 * Transmit(high output power, SMPS mode): PA4=0, PA5=1 */
static const RADIOLIB_PIN_TYPE rfswitch_pins[5] = {PA4, PA5, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC};

static const Module::RfSwitchMode_t rfswitch_table[4] = {
    {STM32WLx::MODE_IDLE, {LOW, LOW}}, {STM32WLx::MODE_RX, {HIGH, LOW}}, {STM32WLx::MODE_TX_HP, {LOW, HIGH}}, END_OF_MODE_TABLE};
