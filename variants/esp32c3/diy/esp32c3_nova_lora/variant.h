#ifndef _VARIANT_ESP32C3_NOVA_LORA_
#define _VARIANT_ESP32C3_NOVA_LORA_

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// Status
#define LED_PIN 8
#define LED_STATE_ON 1 // State when LED is lit

// I2C (Wire) & OLED
#define WIRE_INTERFACES_COUNT (1)
#define I2C_SDA (10)
#define I2C_SCL (20)

#define HAS_SCREEN 1
#define USE_SSD1306
#define DISPLAY_FLIP_SCREEN

// Button
#define HAS_BUTTON 1
#define BUTTON_PIN (9) // BOOT button

#define FSM_ROTARY_ENCODER 1
#define FSM_ROTARY_ENCODER_DEBOUNCE 300
#define FSM_ROTARY_ENCODER_CHECK_INTERVAL 2
#define ROTARY_A (2)
#define ROTARY_B (1)
#define ROTARY_PRESS (9)

// #define USE_VIRTUAL_KEYBOARD 1
#define CANNED_MESSAGE_MODULE_ENABLE 1

// LoRa
#define USE_LLCC68
#define USE_SX1262
// #define USE_RF95
#define USE_SX1268

#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET (0)
#define LORA_DIO1 (3)
#define LORA_BUSY (21)
#define LORA_SCK (4)
#define LORA_MISO (6)
#define LORA_MOSI (5)
#define LORA_CS (7)

#define LORA_RXEN RADIOLIB_NC
#define LORA_TXEN RADIOLIB_NC
#define SX126X_DIO2_AS_RF_SWITCH

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET
#define SX126X_RXEN LORA_RXEN
#define SX126X_TXEN LORA_TXEN

// Power
// Outputting 22dBm from SX1262 results in ~30dBm E22-900M30S output (module only uses last stage of the YP2233W PA)
#define SX126X_MAX_POWER 22 // SX126xInterface.cpp defaults to 22 if not defined, but here we define it for good practice
// #ifdef EBYTE_E22_900M30S
// 10dB PA gain and 30dB rated output; based on measurements from
// https://github.com/S5NC/EBYTE_ESP32-S3/blob/main/E22-900M30S%20power%20output%20testing.txt
// #define TX_GAIN_LORA 7
// #define SX126X_MAX_POWER 22
// #endif

// E22 series TCXO voltage is 1.8V per https://www.ebyte.com/en/pdf-down.aspx?id=781 (source
// https://github.com/jgromes/RadioLib/issues/12#issuecomment-520695575), so set it as such
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
// #define SX126X_DIO3_TCXO_VOLTAGE 2.2
#define TCXO_OPTIONAL // make it so that the firmware can try both TCXO and XTAL

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
