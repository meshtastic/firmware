#ifndef _VARIANT_MAPPi32_
#define _VARIANT_MAPPi32_

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// =======================
// I2C
// =======================
#define WIRE_INTERFACES_COUNT (1)
#define I2C_SDA (21)
#define I2C_SCL (22)

// =======================
// Button
// =======================
#define BUTTON_PIN (0)

// =======================
// LED
// =======================
#define LED_PIN (5)

// =======================
// LoRa (SX1276 - RFM95)
// =======================
#define USE_RF95

#define LORA_SCK   (14)
#define LORA_MISO  (12)
#define LORA_MOSI  (13)
#define LORA_CS    (15)

#define LORA_RESET (0)
#define LORA_DIO0  (27)
#define LORA_DIO1  (26)  // <-- tambahkan ini
#define LORA_DIO2  (25)  // <-- optional, tergantung modul

// =======================
// SPI Config
// =======================
#define LORA_HSPI true

// =======================
// Flash
// =======================
#define BOARD_FLASH_SIZE_MB (16)

// =======================
// Optional Features
// =======================
#define HAS_LED 1
#define HAS_BUTTON 1
#define SUPPORT_OTA true

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif