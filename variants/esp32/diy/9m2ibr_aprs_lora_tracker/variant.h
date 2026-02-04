/*

  9M2IBR APRS LoRa Tracker: ESP32-WROOM-32 + EBYTE E22-400M30S
  https://shopee.com.my/product/1095224/21692283917

  Originally developed for LoRa_APRS_iGate and GPIO is similar to
  https://github.com/richonguzman/LoRa_APRS_iGate/blob/main/variants/ESP32_DIY_1W_LoRa_Mesh_V1_2/board_pinout.h

*/

// OLED (may be different controllers depending on screen size)
#define I2C_SDA 21
#define I2C_SCL 22
#define HAS_SCREEN 1 // Generates randomized BLE pin

// GNSS: Ai-Thinker GP-02 BDS/GNSS module
#define GPS_RX_PIN 16
#define GPS_TX_PIN 17

// Button
#define BUTTON_PIN 15 // Right side button - if not available, set device.button_gpio to 0 from Meshtastic client

// LEDs
#define LED_PIN 13 // Tx LED
#define USER_LED 2 // Rx LED

// Buzzer
#define PIN_BUZZER 33

// Battery sense
#define BATTERY_PIN 35
#define ADC_MULTIPLIER 2.01 // 100k + 100k, and add 1% tolerance
#define ADC_CHANNEL ADC1_GPIO35_CHANNEL
#define BATTERY_SENSE_RESOLUTION_BITS ADC_RESOLUTION

// SPI
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23

// LoRa
#define LORA_CS 5
#define LORA_DIO0 26          // a No connect on the SX1262/SX1268 module
#define LORA_RESET 27         // RST for SX1276, and for SX1262/SX1268
#define LORA_DIO1 12          // IRQ for SX1262/SX1268
#define LORA_DIO2 RADIOLIB_NC // BUSY for SX1262/SX1268
#define LORA_DIO3             // NC, but used as TCXO supply by E22 module
#define LORA_RXEN 32          // RF switch RX (and E22 LNA) control by ESP32 GPIO
#define LORA_TXEN 25          // RF switch TX (and E22 PA) control by ESP32 GPIO

// RX/TX for RFM95/SX127x
#define RF95_RXEN LORA_RXEN
#define RF95_TXEN LORA_TXEN
// #define RF95_TCXO <GPIO#>

// common pinouts for SX126X modules
#define SX126X_CS 5
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_RXEN LORA_RXEN
#define SX126X_TXEN LORA_TXEN

// Support alternative modules if soldered in place of E22
#define USE_RF95 // RFM95/SX127x
#define USE_SX1262
#define USE_SX1268
#define USE_LLCC68

// E22 TCXO support
#ifdef EBYTE_E22
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define TCXO_OPTIONAL // make it so that the firmware can try both TCXO and XTAL
#endif
