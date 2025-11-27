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

// Number of pins defined in PinDescription array
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

// LEDs
#define PIN_LED1 (13) // 13 red (confirmed on 1.0 board)
#define LED_RED PIN_LED1
#define LED_BLUE PIN_LED1
#define LED_GREEN PIN_LED1
#define LED_BUILTIN LED_BLUE
#define LED_CONN LED_BLUE
#define LED_STATE_ON 0 // State when LED is lit

/*
 * Buttons
 */
#define PIN_BUTTON1 (32 + 10)
// #define PIN_BUTTON2 (0 + 18)      // 0.18 is labeled on the board as RESET but we configure it in the bootloader as a regular
// GPIO

/*
No longer populated on PCB
*/
#define PIN_SERIAL2_RX (0 + 7)
#define PIN_SERIAL2_TX (0 + 8)
//  #define PIN_SERIAL2_EN (0 + 17)

/**
    Wire Interfaces
    */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (32 + 15)
#define PIN_WIRE_SCL (32 + 13)

/*
 * Lora radio
 */

#define USE_SX1262
#define SX126X_CS (0 + 26) // FIXME - we really should define LORA_CS instead
#define LORA_CS (0 + 26)
#define SX126X_DIO1 (0 + 16)
// Note DIO2 is attached internally to the module to an analog switch for TX/RX switching
// #define SX1262_DIO3 (0 + 21)
// This is used as an *output* from the sx1262 and connected internally to power the tcxo, do not drive from the
//    main
// CPU?
#define SX126X_BUSY (0 + 15)
#define SX126X_RESET (0 + 12)
// Not really an E22 but TTGO seems to be trying to clone that
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// Display (E-Ink)
#define PIN_EINK_CS 24
#define PIN_EINK_BUSY 32 + 6
#define PIN_EINK_DC 31
#define PIN_EINK_RES 32 + 4
#define PIN_EINK_SCLK 22
#define PIN_EINK_MOSI 20

#define PIN_SPI1_MISO -1
#define PIN_SPI1_MOSI PIN_EINK_MOSI
#define PIN_SPI1_SCK PIN_EINK_SCLK

/*
 * GPS pins
 */

#define PIN_SERIAL1_RX 32 + 5
#define PIN_SERIAL1_TX 32 + 7

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 2

// For LORA, spi 0
#define PIN_SPI_MISO (32 + 9)
#define PIN_SPI_MOSI (0 + 5)
#define PIN_SPI_SCK (0 + 4)

// #define PIN_PWR_EN (0 + 6)

// To debug via the segger JLINK console rather than the CDC-ACM serial device
// #define USE_SEGGER

// Battery
// The battery sense is hooked to pin A0 (4)
// it is defined in the anlaolgue pin section of this file
// and has 12 bit resolution

#define ADC_CTRL 32 + 2
#define ADC_CTRL_ENABLED HIGH
#define BATTERY_PIN 29
#define ADC_RESOLUTION 14

#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER (4.6425F)

#undef HAS_GPS
#define HAS_GPS 0
#define HAS_RTC 0
#ifdef __cplusplus
}
#endif

#endif
