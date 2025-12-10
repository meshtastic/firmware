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

#define LED_PIN PB5
#define LED_STATE_ON 0

#define WIO_E5

#if (defined(LED_BUILTIN) && LED_BUILTIN == PNUM_NOT_DEFINED)
#undef LED_BUILTIN
#define LED_BUILTIN (LED_PIN)
#endif

#endif
