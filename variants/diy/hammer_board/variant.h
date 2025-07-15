// For OLED LCD
#define I2C_SDA 21
#define I2C_SCL 22

// GPS
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 12  // RXD1
#define GPS_TX_PIN 15  // TXD1
#define GPS_UBLOX

#define BUTTON_PIN 0  // Assuming same as T-Beam (adjust if different)
//#define BATTERY_PIN 35 // Assuming same as original (adjust if different)
#define ADC_CHANNEL ADC1_GPIO35_CHANNEL
#define ADC_MULTIPLIER 1.85 // (R1 = 470k, R2 = 680k)
#define EXT_PWR_DETECT 4    // ALM1 used as external power detect
//#define EXT_NOTIFY_OUT 12   // RXD1 (reused, adjust if needed)
//#define LED_PIN 2           // Assuming same as original (adjust if different)

// LoRa Pinout (VSPI)
#define LORA_DIO0 RADIOLIB_NC  // No connect on SX1262/SX1268
#define LORA_RESET 23          // RESET for SX1262/SX1268
#define LORA_DIO1 33           // DIO1 (IRQ) for SX1262/SX1268
#define LORA_DIO2 32           // DIO2 (BUSY) for SX1262/SX1268
#define LORA_DIO3 RADIOLIB_NC  // Not connected, TCXO enabled internally if high

#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_CS 18

// Ethernet (W5500) Pinout (HSPI)
// Ethernet (HSPI)
#define ETH_CS_PIN 16   // Matches ETH_CS
#define ETH_SCK 35
#define ETH_MISO 34
#define ETH_MOSI 25
#define ETH_RST_PIN 17  // Matches ETH_RESET
#define ETH_INT_PIN -1  // Set to -1 if no interrupt pin (W5500 may not need it)
#define ETH_PHY_W5500 0 // Typically 0 for W5500 in ETHClass2.h
#define SPI3_HOST HSPI_HOST // Use HSPI for W5500


// Supported LoRa modules
#define USE_RF95 // RFM95/SX127x
#define USE_SX1262
#define USE_SX1268
#define USE_LLCC68




// Common pinouts for SX126X modules
#define SX126X_CS 18       // NSS for SX126X (LoRa)
#define SX126X_DIO1 33     // LORA_DIO1
#define SX126X_BUSY 32     // LORA_DIO2
#define SX126X_RESET 23    // LORA_RESET
#define SX126X_RXEN 14     // RXEN
#define SX126X_TXEN 13     // TXEN
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define TCXO_OPTIONAL // Support both TCXO and XTAL

// RX/TX for RFM95/SX127x
#define RF95_RXEN 14
#define RF95_TXEN 13

// Set max power for SX126X (e.g., E22 900M30S)
#define SX126X_MAX_POWER 22

#ifdef EBYTE_E22
// Internally hooks SX126x-DIO2 to control TX/RX switch
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define TCXO_OPTIONAL
#endif