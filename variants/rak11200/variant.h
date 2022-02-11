#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define EXTERNAL_NUM_INTERRUPTS 16
#define NUM_DIGITAL_PINS 40
#define NUM_ANALOG_INPUTS 16

#define analogInputToDigitalPin(p) (((p) < 20) ? (esp32_adc2gpio[(p)]) : -1)
#define digitalPinToInterrupt(p) (((p) < 40) ? (p) : -1)
#define digitalPinHasPWM(p) (p < 34)

#define LED_GREEN 12
#define LED_BLUE 2

#define LED_BUILTIN LED_GREEN

static const uint8_t TX = 1;
static const uint8_t RX = 3;

#define TX1 21
#define RX1 19

#define WB_IO1 14
#define WB_IO2 27
#define WB_IO3 26
#define WB_IO4 23
#define WB_IO5 13
#define WB_IO6 22
#define WB_SW1 34
#define WB_A0 36
#define WB_A1 39
#define WB_CS 32
#define WB_LED1 12
#define WB_LED2 2

static const uint8_t SDA = 4;
static const uint8_t SCL = 5;

static const uint8_t SS = 32;
static const uint8_t MOSI = 25;
static const uint8_t MISO = 35;
static const uint8_t SCK = 33;
#endif /* Pins_Arduino_h */

/* -------- Meshtastic pins -------- */
#define I2C_SDA SDA
#define I2C_SCL SCL

#undef GPS_RX_PIN
#define GPS_RX_PIN (RX1)
#undef GPS_TX_PIN
#define GPS_TX_PIN (TX1)

#define LED_PIN LED_BLUE

#define PIN_VBAT WB_A0

// https://docs.rakwireless.com/Product-Categories/WisBlock/RAK13300/

#define LORA_DIO0 26  // a No connect on the SX1262/SX1268 module
#define LORA_RESET WB_IO4 // RST for SX1276, and for SX1262/SX1268
#define LORA_DIO1 WB_IO6  // IRQ for SX1262/SX1268
#define LORA_DIO2 WB_IO5  // BUSY for SX1262/SX1268
#define LORA_DIO3     // Not connected on PCB, but internally on the TTGO SX1262/SX1268, if DIO3 is high the TXCO is enabled

#undef RF95_SCK
#define RF95_SCK SCK
#undef RF95_MISO
#define RF95_MISO MISO
#undef RF95_MOSI
#define RF95_MOSI MOSI
#undef RF95_NSS
#define RF95_NSS SS

#define USE_SX1262
#define SX126X_CS (SS)// NSS for SX126X
#define SX126X_DIO1 (LORA_DIO1)
#define SX126X_BUSY (LORA_DIO2)
#define SX126X_RESET (LORA_RESET)
#define SX126X_TXEN (-1)
#define SX126X_RXEN (WB_IO3)
#define SX126X_E22 // DIO2 controlls an antenna switch and the TCXO voltage is controlled by DIO3
