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

#define BATTERY_PIN AVBAT
// ADC_MULTIPLIER: 3.0 = internal 1:3 bridge divider (DS13105§3.18.3)
// Margin: 1.10 = AVBAT divider tolerance ±10% (Table 82)
#define ADC_MULTIPLIER (1.01f * 3)

#define RAK3172
#define SERIAL_PRINT_PORT 1

#define HAS_LSE 1
#define STM32WL_LSE_DRIVE RCC_LSEDRIVE_LOW

// LoRa
// RAK3172: no TCXO, RAK3172-T: 3.0V TCXO -
// https://github.com/RAKWireless/RAK-STM32-RUI/blob/e5a28be8fab1a492bd9223dd425ca33a8a297d90/variants/WisDuo_RAK3172-T_Board/radio_conf.h#L91
#define TCXO_OPTIONAL
#define SX126X_DIO3_TCXO_VOLTAGE 3.0

#endif
