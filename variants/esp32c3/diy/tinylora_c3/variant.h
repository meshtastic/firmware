#ifndef _VARIANT_TINYLORA_C3_
#define _VARIANT_TINYLORA_C3_

// Board pins configuration for TinyLoRa-C3
// https://github.com/Mymetu/TinyLora-C3

// Button
#define BUTTON_PIN 9

// LED
#define LED_PIN 2
#define LED_STATE_ON 1 // Pulled high when LED is lit.

// Screen & GPS are not present.
#define HAS_SCREEN 0
#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

// LoRa chips
#define USE_LLCC68
#define USE_SX1262
#define USE_SX1268

// SPI
#define LORA_SCK 10
#define LORA_MISO 6
#define LORA_MOSI 7
#define LORA_CS 8

// LoRa
#define LORA_DIO0 RADIOLIB_NC // NC
#define LORA_DIO1 3           // IRQ
#define LORA_DIO2 RADIOLIB_NC // Attached internally to rfswitch.
#define LORA_BUSY 4
#define LORA_RESET 5

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH

/*

Tested modules:
| Mfr          | Module           | TCXO | RF Switch | Notes                                 |
| ------------ | ---------------- | ---- | --------- | ------------------------------------- |
| 255 Mesh     | 255MN-L03        | No   | Int       | LLCC68                                |
| AI-Thinker   | RA-01SC          | No   | Int       | LLCC68                                |
| AI-Thinker   | RA-01SC-P        | No   | Int       | LLCC68,29dbm tx power                 |
| AI-Thinker   | RA-01S           | No   | Int       | SX1268                                |
| AI-Thinker   | RA-01S-P         | No   | Int       | SX1268,29dbm tx power                 |
| AI-Thinker   | RA-01SH          | No   | Int       | SX1262                                |
| Heltec       | HT-RA62          | Yes  | Int       | SX1262                                |

*/

#define SX126X_DIO3_TCXO_VOLTAGE 1.8 // reserved for HT-RA62
#define TCXO_OPTIONAL                // make it so that the firmware can try both TCXO and XTAL

#endif