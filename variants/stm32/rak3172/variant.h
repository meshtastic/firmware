/*
STM32WLE5 Core Module for LoRaWAN® RAK3372
https://store.rakwireless.com/products/wisblock-core-module-rak3372
*/

/*
This variant is a work in progress.
Do not expect a working Meshtastic device with this target.
*/

#ifndef _VARIANT_RAK3172_
#define _VARIANT_RAK3172_

#define USE_STM32WLx

#define LED_POWER PA0 // Green LED
#define LED_STATE_ON 1

#define RAK3172
#define SERIAL_PRINT_PORT 1

// RAK3172 has a built-in 32.768 kHz LSE crystal, so use internal RTC
// Drive level from here:
// https://github.com/RAKWireless/RAK-STM32-RUI/blob/main/variants/WisDuo_RAK3172_Evaluation_Board/rui_inner_main.c
#define HAS_RTC 1
#define RCC_LSEDRIVE_CONFIG RCC_LSEDRIVE_LOW

#endif
