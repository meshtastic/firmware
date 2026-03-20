// T3S3 V1 + MVSR Audio Board variant
// Inherits all T3S3 V1 pin assignments, adds MVSR dual-I2S audio pins

#define HAS_SDCARD
#define SDCARD_USE_SPI1

#define USE_SSD1306

#define BATTERY_PIN 1 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
// ratio of voltage divider = 2.0 (R42=100k, R43=100k)
#define ADC_MULTIPLIER 2.11 // 2.0 + 10% for correction of display undervoltage.
#define ADC_CHANNEL ADC1_GPIO1_CHANNEL

#define I2C_SDA 18 // I2C pins for this board
#define I2C_SCL 17

#define I2C_SDA1 43
#define I2C_SCL1 44

#define LED_POWER 37 // If defined we will blink this LED
#define BUTTON_PIN 0 // If defined, this will be used for user button presses,

#define BUTTON_NEED_PULLUP

// TTGO uses a common pinout for their SX1262 vs RF95 modules - both can be enabled and we will probe at runtime for RF95 and if
// not found then probe for SX1262
#define USE_RF95 // RFM95/SX127x
#define USE_SX1262
#define USE_SX1280
#define USE_LR1121

#define LORA_SCK 5
#define LORA_MISO 3
#define LORA_MOSI 6
#define LORA_CS 7
#define LORA_RESET 8

// per SX1276_Receive_Interrupt/utilities.h
#define LORA_DIO0 9
#define LORA_DIO1 33 // TCXO_EN ?
#define LORA_DIO2 34
#define LORA_RXEN 21
#define LORA_TXEN 10

// per SX1262_Receive_Interrupt/utilities.h
#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 33
#define SX126X_BUSY 34
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif

// per SX128x_Receive_Interrupt/utilities.h
#ifdef USE_SX1280
#define SX128X_CS LORA_CS
#define SX128X_DIO1 9
#define SX128X_DIO2 33
#define SX128X_DIO3 34
#define SX128X_BUSY 36
#define SX128X_RESET LORA_RESET
#define SX128X_RXEN 21
#define SX128X_TXEN 10
#define SX128X_MAX_POWER 3
#endif

// LR1121
#ifdef USE_LR1121
#define LR1121_IRQ_PIN 36
#define LR1121_NRESET_PIN LORA_RESET
#define LR1121_BUSY_PIN LORA_DIO2
#define LR1121_SPI_NSS_PIN LORA_CS
#define LR1121_SPI_SCK_PIN LORA_SCK
#define LR1121_SPI_MOSI_PIN LORA_MOSI
#define LR1121_SPI_MISO_PIN LORA_MISO
#define LR11X0_DIO3_TCXO_VOLTAGE 3.0
#define LR11X0_DIO_AS_RF_SWITCH
#endif

#define HAS_SDCARD // Have SPI interface SD card slot
#define SDCARD_USE_SPI1

// -----------------------------------------------------------------------------
// MVSR Audio Board — Dual I2S configuration
// The MVSR uses separate I2S buses for microphone and speaker.
// -----------------------------------------------------------------------------
#define AUDIO_I2S_DUAL 1

// Microphone (PDM) — I2S_NUM_0, RX only (PDM mode)
#define AUDIO_I2S_MIC_PDM 1     // Use PDM mode for the microphone
#define AUDIO_I2S_MIC_CLK 15   // PDM clock output (routed via I2S WS pin on ESP32-S3)
#define AUDIO_I2S_MIC_DATA 48  // PDM data input
#define AUDIO_I2S_MIC_EN 35    // Mic enable pin (active HIGH)

// Speaker amplifier (MAX98357 or similar) — I2S_NUM_1, TX only
#define AUDIO_I2S_SPK_SCK 40  // BCLK
#define AUDIO_I2S_SPK_WS 41   // LRCLK
#define AUDIO_I2S_SPK_DIN 39  // DATA (serial data to speaker)
#define AUDIO_I2S_SPK_EN 38   // SD_MODE / amplifier enable (active HIGH)

// PTT button — use the T3S3 user button (GPIO 0) by default
// Override in moduleConfig.audio.ptt_pin if using a different pin
#define AUDIO_PTT_PIN 42
