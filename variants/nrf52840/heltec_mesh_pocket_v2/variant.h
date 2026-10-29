#ifndef _VARIANT_HELTEC_MESH_POCKET_V2_
#define _VARIANT_HELTEC_MESH_POCKET_V2_

#define VARIANT_MCK (64000000ul)
#define USE_LFXO

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HAS_SPI_TFT 1
#define HAS_SCREEN 1
#define USE_TFTDISPLAY 1
#define USE_ARDUINO_GFX 1
#define TFT_NV3001B 1
#define HAS_GPS 1
#define HAS_WIRE 1

#define TFT_CS (0 + 16)
#define TFT_SDA (32 + 7)
#define TFT_RST (0 + 24)
#define TFT_RS (32 + 0)
#define TFT_DC TFT_RS
#define TFT_SCL (32 + 1)
#define TFT_EN (0 + 9)
#define TFT_EN_ON LOW
#define TFT_EN_OFF HIGH
#define VTFT_CTRL TFT_EN
#define TFT_BL (0 + 25)
#define TFT_BACKLIGHT_ON HIGH
#define TFT_BACKLIGHT_OFF LOW
#define TFT_WIDTH 128
#define TFT_HEIGHT 220
#define SPI_FREQUENCY 4000000
#define SCREEN_ROTATE
#define TFT_MESH_OVERRIDE COLOR565(255, 255, 128)

#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (0)
#define NUM_ANALOG_OUTPUTS (0)

#define PIN_LED1 (-1)
#define LED_BLUE (-1)
#define LED_GREEN (-1)
#define LED_STATE_ON 1

#define PIN_BUTTON1 (32 + 11)
#define PIN_BUTTON2 (32 + 10)

#define PIN_9006_TX (0 + 15)
#define PIN_9006_IO1 (0 + 4)
#define PIN_9006_IO2 (32 + 9)
#define PIN_9006_RX (0 + 26)
#define PIN_CW_INT (0 + 12)
#define PIN_IO_CSA (0 + 20)
#define PIN_SERIAL2_RX PIN_9006_RX
#define PIN_SERIAL2_TX PIN_9006_TX

#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA (0 + 13)
#define PIN_WIRE_SCL (0 + 5)

#define USE_SX1262
#define SX126X_CS (0 + 14)
#define LORA_CS SX126X_CS
#define SX126X_DIO1 (0 + 31)
#define SX126X_BUSY (0 + 29)
#define SX126X_RESET (0 + 11)
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define SPI_INTERFACES_COUNT 2
#define PIN_SPI_MISO (0 + 2)
#define PIN_SPI_MOSI (32 + 15)
#define PIN_SPI_SCK (32 + 13)

#define PIN_SPI1_MISO (0 + 3)
#define PIN_SPI1_MOSI TFT_SDA
#define PIN_SPI1_SCK TFT_SCL

#define GPS_UC6580
#define GPS_BAUDRATE 115200
#define PIN_GPS_EN (0 + 22)
#define GPS_EN_ACTIVE LOW
#define PIN_GPS_RESET (32 + 6)
#define GPS_RESET_MODE LOW
#define PIN_GPS_PPS (0 + 10)
#define GPS_RX_PIN (32 + 2)
#define GPS_TX_PIN (32 + 4)
#define GPS_THREAD_INTERVAL 50
#define PERIPHERAL_WARMUP_MS 1000

#define PIN_SERIAL1_RX GPS_RX_PIN
#define PIN_SERIAL1_TX GPS_TX_PIN

#ifdef __cplusplus
}
#endif

#endif
