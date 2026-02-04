// From E77-900M22S Product Specification
// https://www.cdebyte.com/pdf-down.aspx?id=2963
// Note 1: PA6 and PA7 pins are used as internal control RF switches of the module, PA6 = RF_TXEN, PA7 = RF_RXEN, RF_TXEN=1
// RF_RXEN=0 is the transmit channel, and RF_TXEN=0 RF_RXEN=1 is the receiving channel

static const RADIOLIB_PIN_TYPE rfswitch_pins[5] = {PA7, PA6, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC};

static const Module::RfSwitchMode_t rfswitch_table[4] = {
    {STM32WLx::MODE_IDLE, {LOW, LOW}}, {STM32WLx::MODE_RX, {HIGH, LOW}}, {STM32WLx::MODE_TX_HP, {LOW, HIGH}}, END_OF_MODE_TABLE};