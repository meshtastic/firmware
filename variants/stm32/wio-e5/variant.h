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

// Wio-E5 has a built-in 32.768 kHz LSE crystal, so use internal RTC
// Drive level from here:
// https://github.com/Seeed-Studio/LoRaWan-E5-Node/blob/main/Projects/Applications/LoRaWAN/LoRaWAN_End_Node/Core/Src/main.c
#define HAS_RTC 1
#define RCC_LSEDRIVE_CONFIG RCC_LSEDRIVE_LOW

#endif
