#pragma once

#define BUTTON_PIN 0

#define I2C_SDA 47
#define I2C_SCL 48

#define USE_POWERSAVE
#define SLEEP_TIME 120

#define HAS_ADS1115
#define ADS1115_ADDR 0x48

// LED controller
#define HAS_LP5814

// ES8311 DAC / AMP
#define HAS_I2S

#define HAS_ES8311
#define DAC_I2S_BCK 11 // SCLK
#define DAC_I2S_WS 12  // LRLK
#define DAC_I2S_DOUT 16
#define DAC_I2S_DIN -1
#define DAC_I2S_MCLK 10

#define HAS_ES7243E
#define ADC_I2S_BCK 11
#define ADC_I2S_WS 12
#define ADC_I2S_DOUT -1
#define ADC_I2S_DIN 15
#define ADC_I2S_MCLK 10

// SX1262 LoRa Module Pins
#define USE_SX1262
#define LORA_SCK 4
#define LORA_MISO 5
#define LORA_MOSI 6
#define LORA_CS 21
#define LORA_RESET 7

#define LORA_DIO1 9
#define LORA_DIO0 -1
#define LORA_DIO2 8
#define LORA_DIO3

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define SX126X_DIO2_AS_RF_SWITCH

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  GPS L76KB
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define GPS_L76K
#ifdef GPS_L76K
#define GPS_TX_PIN 17
#define GPS_RX_PIN 18
#define HAS_GPS 1
#define GPS_THREAD_INTERVAL 50
//#define GPS_EN // TODO: add GPS enable pin control via io/expander 
#endif

