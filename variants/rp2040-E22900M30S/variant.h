// #define RADIOLIB_CUSTOM_ARDUINO 1
// #define RADIOLIB_TONE_UNSUPPORTED 1
// #define RADIOLIB_SOFTWARE_SERIAL_UNSUPPORTED 1

#define ARDUINO_ARCH_AVR

#define USE_SH1106 1

// default I2C pins:
// SDA = 4
// SCL = 5

// Recommended pins for SerialModule:
// txd = 8
// rxd = 9

#define EXT_NOTIFY_OUT 22
#define BUTTON_PIN 24

#define LED_PIN PIN_LED

#define BATTERY_PIN 27
#define ADC_ATTENUATION  ADC_ATTEN_DB_11 // 2_5-> 100mv-1250mv, 11-> 150mv-3100mv for ESP32
                     // ESP32-S2/C3/S3 are different
                     // lower dB for lower voltage rnage
#define ADC_MULTIPLIER  3.035                   // VBATT---200k--pin27---100K---GND

//#define ADC_CHANNEL ADC1_GPIO27_CHANNEL
//#define BAT_FULLVOLT 4200 // with the 5.0 divider, input to BATTERY_PIN is 900mv
//#define BAT_EMPTYVOLT 3000
#undef EXT_PWR_DETECT
#define BATTERY_SENSE_RESOLUTION_BITS ADC_RESOLUTION

#define USE_SX1262

#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

#define SX126X_CS 3         // EBYTE module's NSS pin
#define SX126X_SCK 10       // EBYTE module's SCK pin
#define SX126X_MOSI 11      // EBYTE module's MOSI pin
#define SX126X_MISO 12      // EBYTE module's MISO pin
#define SX126X_RESET 15     // EBYTE module's NRST pin
#define SX126X_BUSY 2       // EBYTE module's BUSY pin
#define SX126X_DIO1 21      // EBYTE module's DIO1 pin
#define SX126X_DIO2 20      // EBYTE module's DIO1 pin
#define SX126X_TXEN 19      // Schematic connects EBYTE module's TXEN pin to MCU
#define SX126X_RXEN 18      // Schematic connects EBYTE module's RXEN pin to MCU
#define SX126X_MAX_POWER 22 // Outputting 22dBm from SX1262 results in ~30dBm E22-900M30S output (module only uses last stage of the YP2233W PA)


#define LORA_DIO0 RADIOLIB_NC
#define LORA_DIO3 RADIOLIB_NC

#ifdef USE_SX1262
#define LORA_CS SX126X_CS     // Compatibility with variant file configuration structure
#define LORA_SCK SX126X_SCK   // Compatibility with variant file configuration structure
#define LORA_MOSI SX126X_MOSI // Compatibility with variant file configuration structure
#define LORA_MISO SX126X_MISO // Compatibility with variant file configuration structure
#define LORA_DIO1 SX126X_DIO1 // Compatibility with variant file configuration structure
#define LORA_DIO2 SX126X_DIO2 // Compatibility with variant file configuration structure
#define LORA_BUSY SX126X_BUSY // Compatibility with variant file configuration structure
#define LORA_RESET SX126X_RESET
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif