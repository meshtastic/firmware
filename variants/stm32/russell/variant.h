#ifndef _VARIANT_RUSSELL_
#define _VARIANT_RUSSELL_

#define USE_STM32WLx

// I/O
#define LED_PIN PA0 // Red LED
#define LED_STATE_ON 1
#define BUTTON_PIN PH3 // Shared with BOOT0
#define BUTTON_NEED_PULLUP
// Charger IC charge/standby pins are open-drain with no hardware pull-up:
// Internal pull-up is needed on STM32 (TODO)
// #define EXT_CHRG_DETECT PA5
// #define EXT_PWR_DETECT PA4

// Bosch Sensortec BME280
#define HAS_SENSOR 1

// CDtop CD-PA1010D
#define ENABLE_HWSERIAL1
#define PIN_SERIAL1_RX PB7
#define PIN_SERIAL1_TX PB6
#define HAS_GPS 1
#define PIN_GPS_STANDBY PA15
#define GPS_RX_PIN PB7
#define GPS_TX_PIN PB6

// LoRa
/*
 * RAK3172   (-20–85°C) -> No TCXO
 * RAK3172-T (-40–85°C) -> 3.0V TCXO
 * https://github.com/RAKWireless/RAK-STM32-RUI/blob/e5a28be8fab1a492bd9223dd425ca33a8a297d90/variants/WisDuo_RAK3172-T_Board/radio_conf.h#L91
 */
#define TCXO_OPTIONAL
#define SX126X_DIO3_TCXO_VOLTAGE 3.0

// Required to avoid Serial1 conflicts due to board definition here:
// https://github.com/stm32duino/Arduino_Core_STM32/blob/main/variants/STM32WLxx/WL54CCU_WL55CCU_WLE4C(8-B-C)U_WLE5C(8-B-C)U/variant_RAK3172_MODULE.h
#define RAK3172

#endif
