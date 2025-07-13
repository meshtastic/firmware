// Unlike many other InkHUD variants, this environment does require its own variant.h file
// This is because the default T114 variant maps SPI1 pins to the optional TFT display, and those pins are not broken out

#ifndef _VARIANT_HELTEC_NRF_
#define _VARIANT_HELTEC_NRF_
/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO // Board uses 32khz crystal for LF

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#define HELTEC_MESH_NODE_T114

// Number of pins defined in PinDescription array
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

// LEDs
#define PIN_LED1 (32 + 3) // green (confirmed on 1.0 board)
#define LED_BLUE PIN_LED1 // fake for bluefruit library
#define LED_GREEN PIN_LED1
#define LED_BUILTIN LED_GREEN
#define LED_STATE_ON 0 // State when LED is lit

#define HAS_NEOPIXEL                         // Enable the use of neopixels
#define NEOPIXEL_COUNT 2                     // How many neopixels are connected
#define NEOPIXEL_DATA 14                     // gpio pin used to send data to the neopixels
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800) // type of neopixels in use

/*
 * Buttons
 */
#define PIN_BUTTON1 (32 + 10)
// #define PIN_BUTTON2 (0 + 18)      // 0.18 is labeled on the board as RESET but we configure it in the bootloader as a regular
// GPIO

/*
No longer populated on PCB
*/
#define PIN_SERIAL2_RX (0 + 9)
#define PIN_SERIAL2_TX (0 + 10)
//  #define PIN_SERIAL2_EN (0 + 17)

/*
 * I2C
 */

#define WIRE_INTERFACES_COUNT 2

// I2C bus 0
// Routed to footprint for PCF8563TS RTC
// Not populated on T114 V1, maybe in future?
#define PIN_WIRE_SDA (0 + 26) // P0.26
#define PIN_WIRE_SCL (0 + 27) // P0.27

// I2C bus 1
// Available on header pins, for general use
#define PIN_WIRE1_SDA (0 + 16) // P0.16
#define PIN_WIRE1_SCL (0 + 13) // P0.13

/*
 * Lora radio
 */

#define USE_SX1262
// #define USE_SX1268
#define SX126X_CS (0 + 24) // FIXME - we really should define LORA_CS instead
#define LORA_CS (0 + 24)
#define SX126X_DIO1 (0 + 20)
// Note DIO2 is attached internally to the module to an analog switch for TX/RX switching
// #define SX1262_DIO3 (0 + 21)
// This is used as an *output* from the sx1262 and connected internally to power the tcxo, do not drive from the
//    main
// CPU?
#define SX126X_BUSY (0 + 17)
#define SX126X_RESET (0 + 25)
// Not really an E22 but TTGO seems to be trying to clone that
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

/*
 * E-Ink DIY
 */
#define PIN_EINK_MOSI (0 + 8) // also called SDA
#define PIN_EINK_SCLK (0 + 7)
#define PIN_EINK_CS (32 + 12)
#define PIN_EINK_DC (32 + 14)
#define PIN_EINK_RES (0 + 5)
#define PIN_EINK_BUSY (32 + 15)

/*
 * GPS pins
 */

#define GPS_L76K

// #define PIN_GPS_RESET (32 + 6) // An output to reset L76K GPS. As per datasheet, low for > 100ms will reset the L76K
#define GPS_RESET_MODE LOW
// #define PIN_GPS_EN (21)
#define VEXT_ENABLE (0 + 21)
#define PERIPHERAL_WARMUP_MS 1000 // Make sure I2C QuickLink has stable power before continuing
#define VEXT_ON_VALUE HIGH
// #define GPS_EN_ACTIVE HIGH
#define PIN_GPS_STANDBY (32 + 2) // An output to wake GPS, low means allow sleep, high means force wake
#define PIN_GPS_PPS (32 + 4)
// Seems to be missing on this new board
// #define PIN_GPS_PPS (32 + 4)  // Pulse per second input from the GPS
#define GPS_TX_PIN (32 + 5) // This is for bits going TOWARDS the CPU
#define GPS_RX_PIN (32 + 7) // This is for bits going TOWARDS the GPS

#define GPS_THREAD_INTERVAL 50

#define PIN_SERIAL1_RX GPS_TX_PIN
#define PIN_SERIAL1_TX GPS_RX_PIN

// PCF8563 RTC Module
#define PCF8563_RTC 0x51

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 2

// For LORA, spi 0
#define PIN_SPI_MISO (0 + 23)
#define PIN_SPI_MOSI (0 + 22)
#define PIN_SPI_SCK (0 + 19)

#define PIN_SPI1_MISO -1
#define PIN_SPI1_MOSI PIN_EINK_MOSI
#define PIN_SPI1_SCK PIN_EINK_SCLK

// #define PIN_PWR_EN (0 + 6)

// To debug via the segger JLINK console rather than the CDC-ACM serial device
// #define USE_SEGGER

// Battery
// The battery sense is hooked to pin A0 (4)
// it is defined in the anlaolgue pin section of this file
// and has 12 bit resolution

#define ADC_CTRL 6
#define ADC_CTRL_ENABLED HIGH
#define BATTERY_PIN 4
#define ADC_RESOLUTION 14

#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER (4.90F)

#define HAS_RTC 0
#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
