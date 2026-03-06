// LilyGo T-Beam-BPF variant.h
// Configuration based on LilyGO utilities.h and RF documentation

// TODO Lock to 2M (144mhz) ham "region"
// #define REGULATORY_LORA_REGIONCODE meshtastic_Config_LoRaConfig_RegionCode_HAM_2M

// I2C for OLED display (SH1106 at 0x3C)
#define I2C_SDA 8
#define I2C_SCL 9

// GPS - Quectel L76K
#define GPS_RX_PIN 5
#define GPS_TX_PIN 6
#define GPS_1PPS_PIN 7
#define HAS_GPS 1
#define GPS_BAUDRATE 9600

// Buttons
#define BUTTON_PIN 0     // BUTTON 1
#define ALT_BUTTON_PIN 3 // BUTTON 2

// SPI (shared by LoRa and SD)
#define SPI_MOSI 11
#define SPI_SCK 12
#define SPI_MISO 13
#define SPI_CS 10

// SD Card
#define HAS_SDCARD
#define SDCARD_USE_SPI1
#define SDCARD_CS SPI_CS

// LoRa Radio - SX1278 144-148Mhz
#define USE_RF95
#define LORA_SCK SPI_SCK
#define LORA_MISO SPI_MISO
#define LORA_MOSI SPI_MOSI
#define LORA_CS 1
#define LORA_RESET 18
#define LORA_IRQ 14
#define LORA_DIO0 LORA_IRQ
#define LORA_DIO1 21
#define RF95_RXEN 39 // LNA enable - HIGH during RX

// CRITICAL: Radio power enable - MUST be HIGH before lora.begin()!
// GPIO 16 powers the SX1278 via LDO
#define RF95_POWER_EN 16

// "+27dBm"? PA! Investigate further (poorly documented)
// LilyGo Docs specify SX1278 power must be capped at 10
#define RF95_MAX_POWER 10
// TODO map PA output curve
// #define TX_GAIN_LORA 17

// Display - SH1106 OLED (128x64)
#define USE_SH1106
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

// 32768 Hz crystal present
#define HAS_32768HZ 1

// PMU
#define HAS_AXP2101
// #define PMU_IRQ 4 // Leave disabled for now
