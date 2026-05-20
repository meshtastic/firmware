/*

 LightTracker Plus is an ESP32-based development board featuring a 1 Watt SX1268 (433MHz) LoRa module, GPS module, battery holder, and 2 dBi 433 MHz SMA antenna.

 https://shop.qrp-labs.com/aprs/lighttrackerplus

  Originally developed for LoRa_APRS_iGate and GPIO is same as
  https://github.com/lightaprs/LightLoRaAPRS/blob/main/variants/light_tracker_plus_v1/variant.h

*/

// OLED (may be different controllers depending on screen size)
#define I2C_SDA 3
#define I2C_SCL 4
#define HAS_SCREEN 1

// GNSS: Quectel L80RE-M37 (GPS-QZSS) module
#define GPS_RX_PIN 17
#define GPS_TX_PIN 18
#define PIN_GPS_EN 33


// Button
#define BUTTON_PIN 0 // Left side button - if not available, set device.button_gpio to 0 from Meshtastic client

// LEDs
#define LED_POWER 16 // Tx LED

// Battery sense
#define BATTERY_PIN 1
#define ADC_MULTIPLIER 5.65
#define ADC_CHANNEL ADC1_CHANNEL_0
#define BATTERY_SENSE_RESOLUTION_BITS ADC_RESOLUTION
#define ADC_REFERENCE REF_3V3

// SPI
#define LORA_SCK 12
#define LORA_MISO 13
#define LORA_MOSI 11

// LoRa
#define LORA_CS 10
#define LORA_DIO0 5          // a No connect on the SX1262/SX1268 module
#define LORA_RESET 9         // RST for SX1276, and for SX1262/SX1268
#define LORA_DIO1 5          // IRQ for SX1262/SX1268
#define LORA_DIO2 RADIOLIB_NC // BUSY for SX1262/SX1268
#define LORA_DIO3             // NC, but used as TCXO supply by E22 module
#define LORA_RXEN 42          // RF switch RX (and E22 LNA) control by ESP32 GPIO
#define LORA_TXEN 14          // RF switch TX (and E22 PA) control by ESP32 GPIO


// common pinouts for SX126X modules
#define SX126X_CS 10
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_RXEN LORA_RXEN
#define SX126X_TXEN LORA_TXEN
#define SX126X_POWER_EN 21

#define USE_SX1268
#define SX126X_MAX_POWER 22

// E22 TCXO support
#ifdef EBYTE_E22
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define TCXO_OPTIONAL // make it so that the firmware can try both TCXO and XTAL

#endif
