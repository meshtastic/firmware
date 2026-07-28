// Heltec WiFi LoRa 32 V4-R2 — ESP32-S3 + SX1262
// External 2.8" ILI9341V SPI display (240x320) + FT6336G I2C capacitive touch
// No on-board LCD screen

#define VEXT_ENABLE 36 // active low, powers the LoRa antenna boost
#define VEXT_ON_VALUE LOW
#define BUTTON_PIN 0

#define ADC_CTRL 37
#define ADC_CTRL_ENABLED HIGH
#define BATTERY_PIN 1 // ADC1_CH0 — battery voltage via divider; pull ADC_CTRL high to read
#define ADC_CHANNEL ADC_CHANNEL_0
#define ADC_ATTENUATION ADC_ATTEN_DB_2_5
#define ADC_MULTIPLIER 4.9 * 1.045

#define USE_SX1262

#define LORA_DIO0 -1 // not connected on SX1262
#define LORA_RESET 12 // SX1262 RESET
#define LORA_DIO1 14 // SX1262 IRQ
#define LORA_DIO2 13 // SX1262 BUSY
#define LORA_DIO3    // not connected on PCB

#define LORA_SCK 9
#define LORA_MISO 11
#define LORA_MOSI 10
#define LORA_CS 8

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET

#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// Enable Traffic Management Module
#ifndef HAS_TRAFFIC_MANAGEMENT
#define HAS_TRAFFIC_MANAGEMENT 1
#endif

// ---- GC1109 RF FRONT END CONFIGURATION (V4.2) ----
// CTX -> SX1262 DIO2 (automatic via SX126X_DIO2_AS_RF_SWITCH)
// CSD -> GPIO2, CPS -> GPIO46, VCC -> GPIO7
#define LORA_PA_POWER 7         // VFEM_Ctrl
#define LORA_GC1109_PA_EN 2     // CSD — chip enable (HIGH=on)
#define LORA_GC1109_PA_TX_EN 46 // CPS — PA mode (HIGH=full PA)

// ---- KCT8103L RF FRONT END CONFIGURATION (V4.3) ----
// CPS -> SX1262 DIO2 (automatic via SX126X_DIO2_AS_RF_SWITCH)
// CSD -> GPIO2, CTX -> GPIO5, VCC -> GPIO7
#define LORA_KCT8103L_PA_CSD 2 // CSD — chip enable (HIGH=on)
#define LORA_KCT8103L_PA_CTX 5 // CTX — RX bypass (HIGH) / RX LNA (LOW)

/*
 * GPS pins — L76K GNSS module
 */
#define GPS_L76K
#define PIN_GPS_RESET (42) // GNSS_RESET
#define GPS_RESET_MODE LOW
#define PIN_GPS_EN (34) // **LCD_CS**
#define GPS_EN_ACTIVE LOW
#define PERIPHERAL_WARMUP_MS 1000
#define PIN_GPS_STANDBY (40)  // GNSS_WAKEUP
#define PIN_GPS_PPS (41) // GNSS_PPS
#define GPS_TX_PIN (38) // GNSS → CPU (GNSS_RX)
#define GPS_RX_PIN (39) // CPU → GNSS (GNSS_TX)
#define GPS_THREAD_INTERVAL 50

/*
 * External display connections (set via LGFX_PIN_* in platformio.ini)
 *
 * Display SPI (SPI3_HOST):
 *   SCK  → GPIO47   LCD_RS/DC → GPIO21
 *   MOSI → GPIO33   CS        → GPIO34
 *   MISO → NC (-1)  RST       → GPIO48     [GPIO26 is PSRAM CS — do not use]
 *   BL   → GPIO35 
 *
 * Touch I2C (Wire, port 0 — GPIO3/4):
 *   SDA → GPIO4   RST → GPIO45
 *   SCL → GPIO3   INT → not connected (polling)
 */

// I2C bus used for touch and peripheral scan
#define I2C_SDA 4
#define I2C_SCL 3
