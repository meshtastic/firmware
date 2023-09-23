// For OLED LCD
#define I2C_SDA 21
#define I2C_SCL 22

// GPS
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 12
#define GPS_TX_PIN 15
#define GPS_UBLOX
#define PIN_GPS_EN 4

#define BUTTON_PIN 39  // The middle button GPIO on the T-Beam
#define BATTERY_PIN 35 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
#define ADC_CHANNEL ADC1_GPIO35_CHANNEL
#define ADC_MULTIPLIER 1.85 // (R1 = 470k, R2 = 680k)
#define EXT_PWR_DETECT 4    // Pin to detect connected external power source for LILYGO® TTGO T-Energy T18 and other DIY boards
#define EXT_NOTIFY_OUT 12   // Overridden default pin to use for Ext Notify Module (#975).
#define LED_PIN 2           // add status LED (compatible with core-pcb and DIY targets)

#define LORA_DIO0 26  // a No connect on the SX1262/SX1268 module
#define LORA_RESET 23 // RST for SX1276, and for SX1262/SX1268
#define LORA_DIO1 33  // IRQ for SX1262/SX1268
#define LORA_DIO2 32  // BUSY for SX1262/SX1268
#define LORA_DIO3     // Not connected on PCB, but internally on the TTGO SX1262/SX1268, if DIO3 is high the TXCO is enabled

#define RF95_SCK 5
#define RF95_MISO 19
#define RF95_MOSI 27
#define RF95_NSS 18

#define USE_SX1262

#define SX126X_CS 18 // NSS for SX126X
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_RXEN 14
#define SX126X_TXEN RADIOLIB_NC
#define SX126X_DIO2_AS_RF_SWITCH

// Set lora.tx_power to 13 for Hydra or other E22 900M30S target due to PA
#define SX126X_MAX_POWER 13

#define SX126X_DIO3_TCXO_VOLTAGE 1.8
