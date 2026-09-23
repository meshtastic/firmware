#ifndef _MESHSMITH_PHOTON_NRF52_H_
#define _MESHSMITH_PHOTON_NRF52_H_

#define VARIANT_MCK (64000000ul)
#define USE_LFXO

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PINS_COUNT (33)
#define NUM_DIGITAL_PINS (33)
#define NUM_ANALOG_INPUTS (8)
#define NUM_ANALOG_OUTPUTS (0)

#define D0 (0ul)
#define D1 (1ul)
#define D2 (2ul)
#define D3 (3ul)
#define D4 (4ul)
#define D5 (5ul)
#define D6 (6ul)
#define D7 (7ul)
#define D8 (8ul)
#define D9 (9ul)
#define D10 (10ul)

#define PIN_A0 (0)
#define PIN_A1 (1)
#define PIN_A2 (32)
#define PIN_A3 (3)
#define PIN_A4 (4)
#define PIN_A5 (5)
#define PIN_VBAT (32)
#define VBAT_ENABLE (14)

static const uint8_t A0 = PIN_A0;
static const uint8_t A1 = PIN_A1;
static const uint8_t A2 = PIN_A2;
static const uint8_t A3 = PIN_A3;
static const uint8_t A4 = PIN_A4;
static const uint8_t A5 = PIN_A5;
#define ADC_RESOLUTION 12

#define LED_STATE_ON (0)
#define LED_RED (11)
#define LED_GREEN (13)
#define LED_BLUE (12)
#define PIN_LED1 LED_GREEN
#define PIN_LED2 LED_BLUE
#define PIN_LED3 LED_RED
#define LED_BUILTIN LED_RED

#define HICHG (22)
#define EXT_CHRG_DETECT (23)
#define EXT_CHRG_DETECT_VALUE LOW

#define PIN_SERIAL2_RX (-1)
#define PIN_SERIAL2_TX (-1)

/*
 * Photon-1W LoRa wiring.
 * Source: MeshSmith Photon nRF52 board support.
 *
 * E22-900M30S contains an SX1262 plus external PA.
 * MeshSmith's implementation uses 20 dBm SX1262 drive for ~30 dBm module output.
 */
#define USE_SX1262
#define SX126X_CS D3
#define SX126X_DIO1 D0
#define SX126X_BUSY D2
#define SX126X_RESET D1
#define SX126X_RXEN RADIOLIB_NC
#define SX126X_TXEN RADIOLIB_NC
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// Photon-specific external PA calibration/safety limit.
// 20 dBm SX1262 drive -> approximately 30 dBm E22 module output.
#define TX_GAIN_LORA 10
#define SX126X_MAX_POWER 20

#define SPI_INTERFACES_COUNT 1
#define PIN_SPI_MISO D9
#define PIN_SPI_MOSI D10
#define PIN_SPI_SCK D8
static const uint8_t SS = SX126X_CS;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

/* Photon onboard GPS */
#define HAS_GPS 1
#define GPS_TX_PIN D6
#define GPS_RX_PIN D7
#define GPS_BAUDRATE 115200
#define GPS_THREAD_INTERVAL 50
#define PIN_SERIAL1_TX GPS_TX_PIN
#define PIN_SERIAL1_RX GPS_RX_PIN

/* Battery fallback: XIAO VBAT ADC.
 * Photon MAX17048 fuel-gauge telemetry is not yet ported in this test target.
 */
#define BATTERY_PIN PIN_VBAT
#define ADC_MULTIPLIER (3)
#define ADC_CTRL VBAT_ENABLE
#define ADC_CTRL_ENABLED LOW
#define BATTERY_SENSE_RESOLUTION_BITS (10)

#define I2C_NO_RESCAN
#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA D4
#define PIN_WIRE_SCL D5
static const uint8_t SDA = PIN_WIRE_SDA;
static const uint8_t SCL = PIN_WIRE_SCL;

#ifdef __cplusplus
}
#endif

#endif
