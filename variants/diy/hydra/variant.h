// For OLED LCD
#define I2C_SDA 21
#define I2C_SCL 22

// For GPS, 'undef's not needed
#define GPS_TX_PIN 15
#define GPS_RX_PIN 12
#define PIN_GPS_EN 4
#define GPS_POWER_TOGGLE // Moved definition from platformio.ini to here

#define BUTTON_PIN 39 // The middle button GPIO on the T-Beam
// Note: On the ESP32 base version, gpio34-39 are input-only, and do not have internal pull-ups.
// If 39 is not being used for a button, it is suggested to remove the #define.
#define BATTERY_PIN 35 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
#define ADC_CHANNEL ADC1_GPIO35_CHANNEL
#define ADC_MULTIPLIER 1.85 // (R1 = 470k, R2 = 680k)
#define EXT_PWR_DETECT 4    // Pin to detect connected external power source for LILYGO® TTGO T-Energy T18 and other DIY boards
#define EXT_NOTIFY_OUT 12   // Overridden default pin to use for Ext Notify Module (#975).
#define LED_PIN 2           // add status LED (compatible with core-pcb and DIY targets)

// Radio
#define USE_SX1262 // E22-900M30S uses SX1262
#define USE_SX1268 // E22-400M30S uses SX1268
#define SX126X_MAX_POWER                                                                                                         \
    22 // Outputting 22dBm from SX1262 results in ~30dBm E22-900M30S output (module only uses last stage of the YP2233W PA)
#define SX126X_DIO3_TCXO_VOLTAGE 1.8 // E22 series TCXO reference voltage is 1.8V

#define SX126X_CS 18    // EBYTE module's NSS pin
#define SX126X_SCK 5    // EBYTE module's SCK pin
#define SX126X_MOSI 27  // EBYTE module's MOSI pin
#define SX126X_MISO 19  // EBYTE module's MISO pin
#define SX126X_RESET 23 // EBYTE module's NRST pin
#define SX126X_BUSY 32  // EBYTE module's BUSY pin
#define SX126X_DIO1 33  // EBYTE module's DIO1 pin

#define SX126X_TXEN 13 // Schematic connects EBYTE module's TXEN pin to MCU
#define SX126X_RXEN 14 // Schematic connects EBYTE module's RXEN pin to MCU

#define LORA_CS SX126X_CS       // Compatibility with variant file configuration structure
#define LORA_SCK SX126X_SCK     // Compatibility with variant file configuration structure
#define LORA_MOSI SX126X_MOSI   // Compatibility with variant file configuration structure
#define LORA_MISO SX126X_MISO   // Compatibility with variant file configuration structure
#define LORA_DIO1 SX126X_DIO1   // Compatibility with variant file configuration structure
#define LORA_TXEN SX126X_TXEN   // Compatibility with variant file configuration structure
#define LORA_RXEN SX126X_RXEN   // Compatibility with variant file configuration structure
#define LORA_RESET SX126X_RESET // Compatibility with variant file configuration structure
#define LORA_DIO2 SX126X_BUSY   // Compatibility with variant file configuration structure