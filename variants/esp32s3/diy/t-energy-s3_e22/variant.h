// NanoVHF T-Energy-S3 + E22(0)-xxxM - DIY
// https://github.com/NanoVHF/Meshtastic-DIY/tree/main/PCB/ESP-32-devkit_EBYTE-E22/Mesh-v1.06-TTGO-T18

// Battery
#define BATTERY_PIN 3
#define ADC_MULTIPLIER 2.0
#define ADC_CHANNEL ADC1_GPIO3_CHANNEL

// Button on NanoVHF PCB
#define BUTTON_PIN 39

// I2C via connectors on NanoVHF PCB
#define I2C_SCL 2
#define I2C_SDA 42

// Screen (disabled)
#define HAS_SCREEN 0 // Assume no screen present by default to prevent crash...

// GPS via T-Energy-S3 onboard connector
#define HAS_GPS 1
#define GPS_TX_PIN 43
#define GPS_RX_PIN 44

// LoRa
#define USE_SX1262 // E22-900M30S, E22-900M22S, and E22-900MM22S (not E220!) use SX1262
#define USE_SX1268 // E22-400M30S, E22-400M33S, E22-400M22S, and E22-400MM22S use SX1268

#define SX126X_MAX_POWER 22          // SX126xInterface.cpp defaults to 22 if not defined, but here we define it for good practice
#define SX126X_DIO3_TCXO_VOLTAGE 1.8 // E22 series TCXO reference voltage is 1.8V

#define SX126X_CS 5    // EBYTE module's NSS pin // FIXME: rename to SX126X_SS
#define SX126X_SCK 6   // EBYTE module's SCK pin
#define SX126X_MOSI 13 // EBYTE module's MOSI pin
#define SX126X_MISO 4  // EBYTE module's MISO pin
#define SX126X_RESET 1 // EBYTE module's NRST pin
#define SX126X_BUSY 48 // EBYTE module's BUSY pin
#define SX126X_DIO1 47 // EBYTE module's DIO1 pin

#define SX126X_TXEN 10 // Schematic connects EBYTE module's TXEN pin to MCU
#define SX126X_RXEN 12 // Schematic connects EBYTE module's RXEN pin to MCU

#define LORA_CS SX126X_CS     // Compatibility with variant file configuration structure
#define LORA_SCK SX126X_SCK   // Compatibility with variant file configuration structure
#define LORA_MOSI SX126X_MOSI // Compatibility with variant file configuration structure
#define LORA_MISO SX126X_MISO // Compatibility with variant file configuration structure
#define LORA_DIO1 SX126X_DIO1 // Compatibility with variant file configuration structure
