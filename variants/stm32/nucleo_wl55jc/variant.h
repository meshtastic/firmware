/*
ST Nucleo-WL55JC (MB1389)
https://www.st.com/en/evaluation-tools/nucleo-wl55jc.html
*/

#ifndef _VARIANT_NUCLEO_WL55JC_
#define _VARIANT_NUCLEO_WL55JC_

#define USE_STM32WLx

// Pin mappings from UM2592: User Manual, STM32WL Nucleo-64 board (MB1389)
// https://www.st.com/resource/en/user_manual/um2592-stm32wl-nucleo64-board-mb1389-stmicroelectronics.pdf

// Human-readable pin macros from variant_NUCLEO_WL55JC1.h

// UM2592 S6.6.1: LEDs
#define LED_POWER LED_GREEN
#define LED_STATE_ON 1
#define LED_LORA LED_RED
#define LED_NOTIFICATION LED_BLUE

// UM2592 S6.6.2: Push-buttons
#define BUTTON_PIN B1_BTN // WKUP1-capable
#define BUTTON_NEED_PULLUP
#define ALT_BUTTON_PIN B2_BTN
#define CANCEL_BUTTON_PIN B3_BTN
#define CANCEL_BUTTON_ACTIVE_LOW true
#define CANCEL_BUTTON_ACTIVE_PULLUP true

// UM2592 S7.4: Arduino UNO R3 connectors - SPI
// Arduino UNO R3 header: CS/D10, MOSI/D11, MISO/D12, SCK/D13
#define PIN_SPI_MOSI PA7
#define PIN_SPI_MISO PA6
#define PIN_SPI_SCK PA5

// UM2592 S7.4: Arduino UNO R3 connectors - UART (GPS, etc.)
// Arduino UNO R3 header: RX/D0, TX/D1
#define PIN_SERIAL2_TX PB6
#define PIN_SERIAL2_RX PB7

// UM2592 S7.4: Arduino UNO R3 connectors - I2C
// Arduino UNO R3 header: SDA/D14, SCL/D15
#define PIN_WIRE_SDA PA11
#define PIN_WIRE_SCL PA12

// RM0453 S18.10: Battery voltage monitoring
// Internal VBAT ADC channel; VBAT bridged to VDD_SYS by SB21
#define BATTERY_PIN AVBAT
#define ADC_MULTIPLIER (1.01f * 3)

// UM2592 S6.5.2: LSE clock
#define HAS_LSE 1
#define STM32WL_LSE_DRIVE RCC_LSEDRIVE_LOW

// UM2592 S6.5.1: HSE clock (used for sub-GHz radio as well)
// NDK NT2016SF-32M-END5875A
#define SX126X_DIO3_TCXO_VOLTAGE 1.7

#endif
