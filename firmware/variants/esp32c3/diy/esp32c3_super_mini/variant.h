#ifndef _VARIANT_ESP32C3_SUPER_MINI_
#define _VARIANT_ESP32C3_SUPER_MINI_

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// I2C (Wire) & OLED
#define WIRE_INTERFACES_COUNT (1)
#define I2C_SDA (1)
#define I2C_SCL (0)

#define USE_SSD1306

// GPS
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN (20)
#define GPS_TX_PIN (21)

// Button
#define BUTTON_PIN (9) // BOOT button

// LoRa
#define USE_LLCC68
#define USE_SX1262
// #define USE_RF95
#define USE_SX1268

#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET (5)
#define LORA_DIO1 (3)
#define LORA_RXEN (2)
#define LORA_BUSY (4)
#define LORA_SCK (10)
#define LORA_MISO (6)
#define LORA_MOSI (7)
#define LORA_CS (8)

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET
#define SX126X_RXEN LORA_RXEN

#define SX126X_DIO3_TCXO_VOLTAGE (1.8)
#define TCXO_OPTIONAL // make it so that the firmware can try both TCXO and XTAL

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
