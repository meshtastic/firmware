#ifndef _VARIANT_ESP32C3_ZERO_
#define _VARIANT_ESP32C3_ZERO_

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// I2C (Wire)
#define I2C_SDA (1)
#define I2C_SCL (0)

#define HAS_SCREEN 0

// GPS
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN (20)
#define GPS_TX_PIN (21)

// Button
#define BUTTON_PIN (9) // BOOT button

// Ai-Thinker Ra-02 (SX1278)
#define USE_RF95
#define LORA_SCK (4)
#define LORA_MISO (5)
#define LORA_MOSI (6)
#define LORA_CS (7)
#define LORA_RESET (8)
#define LORA_DIO0 (3)
#define LORA_DIO1 RADIOLIB_NC
#define LORA_DIO2 RADIOLIB_NC

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
