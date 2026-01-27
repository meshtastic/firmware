#ifndef _VARIANT_MILESIGHT_GS301_
#define _VARIANT_MILESIGHT_GS301_

#define USE_STM32WLx

// I/O
#define LED_STATE_ON 1
#define PIN_LED1 PA0 // Green LED
#define LED_PIN PIN_LED1
#define PIN_LED2 PA0 // Red LED
#define USER_LED PIN_LED2
#define BUTTON_PIN PC13
#define BUTTON_ACTIVE_LOW true
#define BUTTON_ACTIVE_PULLUP false
#define PIN_BUZZER PA6

// EC Sense DGM10 Double Gas Module
// Analog ADuCM355 (unsupported); SHTC3 is connected to ADuCM355 and not directly accessible
#define PIN_WIRE_SDA PB7
#define PIN_WIRE_SCL PB8
// Commented out to keep sensor powered down due to lack of support
/*
#define VEXT_ENABLE PB12 // TI TPS61291DRV VSEL - set LOW before ENable for Vout = 3.3V
#define VEXT_ON_VALUE LOW
#define SENSOR_POWER_CTRL_PIN PB2 // TI TPS61291DRV EN pin
#define SENSOR_POWER_ON HIGH
#define HAS_SENSOR 1
*/

#define ENABLE_HWSERIAL1
#define PIN_SERIAL1_RX NC
#define PIN_SERIAL1_TX PB6

// LoRa
#define SX126X_DIO3_TCXO_VOLTAGE 3.0

// Required to avoid Serial1 conflicts due to board definition here:
// https://github.com/stm32duino/Arduino_Core_STM32/blob/main/variants/STM32WLxx/WL54CCU_WL55CCU_WLE4C(8-B-C)U_WLE5C(8-B-C)U/variant_RAK3172_MODULE.h
#define RAK3172

#endif
