#pragma once

#define BUTTON_PIN 0

#define I2C_SDA 47
#define I2C_SCL 48

// ES8311 DAC / AMP
#define HAS_I2S
#define DAC_I2S_BCK 11 // SCLK
#define DAC_I2S_WS 12  // LRLK
#define DAC_I2S_DOUT 16
#define DAC_I2S_DIN -1
#define DAC_I2S_MCLK 10

#define ADC_I2S_BCK 11
#define ADC_I2S_WS 12
#define ADC_I2S_DOUT -1
#define ADC_I2S_DIN 15
#define ADC_I2S_MCLK 10

// SX1262 LoRa Module Pins
#define USE_SX1262
#define LORA_MISO 5
#define LORA_SCK 4
#define LORA_MOSI 6
#define LORA_CS 21

#define SX126X_CS LORA_CS
#define SX126X_DIO1 9
#define SX126X_BUSY 8
#define SX126X_RESET 7
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define SX126X_RXEN RADIOLIB_NC
#define SX126X_TXEN RADIOLIB_NC
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

