#ifndef _VARIANT_ESP32C3_SUPER_MINI_TENSTAR_
#define _VARIANT_ESP32C3_SUPER_MINI_TENSTAR_

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// I2C (Wire) & OLED
#define WIRE_INTERFACES_COUNT (1)
#define I2C_SDA (8)
#define I2C_SCL (9)

#define USE_SSD1306

// GPS
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN (20)
#define GPS_TX_PIN (21)

// Button, active LOW
#define BUTTON_PIN (10)

// LoRa
#define USE_LLCC68
// TODO: why enable SX126x if not used ?
// Note: without these, LoRa will not work correctly
#define USE_SX1262
// #define USE_RF95
#define USE_SX1268

#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET (0)
#define LORA_DIO1 (1)
#define LORA_RXEN (2)
#define LORA_BUSY (3)
#define LORA_SCK (4)
#define LORA_MISO (5)
#define LORA_MOSI (6)
#define LORA_CS (7)

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
