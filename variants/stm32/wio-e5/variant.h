/*
Wio-E5 mini (formerly LoRa-E5 mini)
https://www.seeedstudio.com/LoRa-E5-mini-STM32WLE5JC-p-4869.html
https://www.seeedstudio.com/LoRa-E5-Wireless-Module-p-4745.html
*/

/*
This variant is a work in progress.
Do not expect a working Meshtastic device with this target.
*/

#ifndef _VARIANT_WIOE5_
#define _VARIANT_WIOE5_

#define USE_STM32WLx

#define LED_POWER PB5
#define LED_STATE_ON 0

#define WIO_E5

// LoRa
// https://github.com/Seeed-Studio/LoRaWan-E5-Node/blob/163c05379b1805dd8f2c061d4557a69985acc953/Middlewares/Third_Party/SubGHz_Phy/stm32_radio_driver/radio_driver.c#L94
#define SX126X_DIO3_TCXO_VOLTAGE 1.7

#endif
