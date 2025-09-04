// Define SS for compatibility with libraries expecting a default SPI chip select pin

#define ARDUINO_ARCH_AVR

#define EXT_NOTIFY_OUT 0xFFFFFFFF
#define BUTTON_PIN 0xFFFFFFFF

#define LED_PIN PIN_LED

#define USE_RF95 // RFM95/SX127x

#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

// https://gitlab.com/invectorlabs/hw/challenger_rp2040_lora
#define LORA_SCK 10  // Clock
#define LORA_CS 9    // Chip Select
#define LORA_MOSI 11 // Serial Data Out
#define LORA_MISO 12 // Serial Data In

#define LORA_RESET 13 // Reset

#define LORA_DIO0 14         // DIO0
#define LORA_DIO1 15         // DIO1
#define LORA_DIO2 18         // DIO2
#define LORA_DIO3 0xFFFFFFFF // Not connected
#define LORA_DIO4 0xFFFFFFFF // Not connected
#define LORA_DIO5 0xFFFFFFFF // Not connected

#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
// #define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif