#pragma once

#define LED_POWER 46
#define LED_STATE_ON 1

#define BUTTON_PIN 0
#define BUTTON_NEED_PULLUP

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

// External expansion chip TCA9555/PCA9555
#define USE_PCA95X5
#define PCA95X5_CLS Pca9555
#define PCA95X5_INC "Pca9555.h"
#define BOARD_PCA9535_ADDR 0x21
#define BOARD_PCA9535_INT 45 // wake from esp light sleep
// Button
#define EXPANDS_BTN_WAKE_UP (0) // INPUT
// I2C
#define EXPANDS_I2C_0_INT (1) // INPUT
// LED
#define EXPANDS_LED_USER (10)
// Display
#define EXPANDS_LCD_PWR_EN (5)
#define EXPANDS_LCD_RST (6)
#define EXPANDS_LCD_CS (4)
#define EXPANDS_TP_RST (8)
#define EXPANDS_TP_INT (3) // INPUT
// SD card
#define EXPANDS_SD_PWR_EN (14)
#define EXPANDS_SD_DETECT (2) // INPUT
// GNSS
#define EXPANDS_GNSS_PWR_EN (13)
#define EXPANDS_GNSS_RST (9)
// USB
#define EXPANDS_EXP_OTG_EN (11)
// Audio
#define EXPANDS_PA_PWR_EN (12)
#define AUDIO_AMP_SETTLE_MS 250
#define AUDIO_AMP_ENABLE(on)                                                                                                     \
    spiLock->lock();                                                                                                             \
    io.digitalWrite(EXPANDS_PA_PWR_EN, (on) ? HIGH : LOW);                                                                       \
    spiLock->unlock();

// Battery
#define EXPANDS_BAT_ADC_EN (15)
// Grove
#define EXPANDS_GROVE_PWR_EN (7)

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
#endif

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  Display (NV3031B + QSPI via SPI3) - BaseUI adaptation
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#if HAS_TFT
#define HAS_SPI_TFT 1
#define HAS_TOUCHSCREEN 1
#define USE_TFTDISPLAY 1 // Enable legacy BaseUI TFTDisplay.cpp build
#define TFT_WIDTH 320    // Required by BaseUI setGeometry
#define TFT_HEIGHT 240   // Required by BaseUI setGeometry
#define USE_VIRTUAL_KEYBOARD 1
#endif

// Battery
#define OCV_ARRAY 4180, 4040, 3864, 3800, 3745, 3710, 3687, 3663, 3623, 3482, 3300
