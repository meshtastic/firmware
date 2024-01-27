#ifndef _VARIANT_RAK4630_
#define _VARIANT_RAK4630_

#define RAK4630

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO // Board uses 32khz crystal for LF
// define USE_LFRC    // Board uses RC for LF

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
#define NUM_ANALOG_INPUTS (6)
#define NUM_ANALOG_OUTPUTS (0)

// LEDs
#define PIN_LED1 (35)
#define PIN_LED2 (36)

#define LED_BUILTIN PIN_LED1
#define LED_CONN PIN_LED2

#define LED_GREEN PIN_LED1
#define LED_BLUE PIN_LED2

#define LED_STATE_ON 1 // State when LED is litted

/*
 * Buttons
 */

#define PIN_BUTTON1 9 // Pin for button on E-ink button module or IO expansion
#define BUTTON_NEED_PULLUP
// #define PIN_BUTTON2 12

/*
 * Analog pins
 */
#define PIN_A0 (-1) //(5)
#define PIN_A1 (31)
#define PIN_A2 (28)
#define PIN_A3 (29)
#define PIN_A4 (30)
#define PIN_A5 (31)
#define PIN_A6 (0xff)
#define PIN_A7 (0xff)

static const uint8_t A0 = PIN_A0;
static const uint8_t A1 = PIN_A1;
static const uint8_t A2 = PIN_A2;
static const uint8_t A3 = PIN_A3;
static const uint8_t A4 = PIN_A4;
static const uint8_t A5 = PIN_A5;
static const uint8_t A6 = PIN_A6;
static const uint8_t A7 = PIN_A7;
#define ADC_RESOLUTION 14

// Other pins
#define PIN_AREF (2)
// #define PIN_NFC1 (9)
// #define PIN_NFC2 (10)

static const uint8_t AREF = PIN_AREF;

/*
 * Serial interfaces
 */
#define PIN_SERIAL1_RX (-1)
#define PIN_SERIAL1_TX (-1)

// Connected to Jlink CDC
#define PIN_SERIAL2_RX (-1)
#define PIN_SERIAL2_TX (-1)

// Testing USB detection
#define NRF_APM

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 2

#define PIN_SPI_MISO (45)
#define PIN_SPI_MOSI (44)
#define PIN_SPI_SCK (43)

#define PIN_SPI1_MISO (-1)
#define PIN_SPI1_MOSI (0 + 13)
#define PIN_SPI1_SCK (0 + 14)

static const uint8_t SS = 42;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

/*
 * eink display pins
 */

#define USE_EINK

#define PIN_EINK_CS (0 + 16)   // TX1
#define PIN_EINK_BUSY (0 + 15) // RX1
#define PIN_EINK_DC (0 + 17)   // IO1
// #define PIN_EINK_RES  (-1)     //first try without RESET then connect it to AIN (AIN0 5 )
#define PIN_EINK_RES (0 + 5)   // 2.13 BN Display needs RESET
#define PIN_EINK_SCLK (0 + 14) // SCL
#define PIN_EINK_MOSI (0 + 13) // SDA

// Controls power for the eink display - Board power is enabled either by VBUS from USB or the CPU asserting PWR_ON
// FIXME - I think this is actually just the board power enable - it enables power to the CPU also
// #define PIN_EINK_PWR_ON (-1)

// RAKRGB
#define HAS_NCP5623

/*
 * Wire Interfaces
 */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (13)
#define PIN_WIRE_SCL (14)

/* @note RAK5005-O GPIO mapping to RAK4631 GPIO ports
   RAK5005-O <->  nRF52840
   IO1       <->  P0.17 (Arduino GPIO number 17)
   IO2       <->  P1.02 (Arduino GPIO number 34)
   IO3       <->  P0.21 (Arduino GPIO number 21)
   IO4       <->  P0.04 (Arduino GPIO number 4)
   IO5       <->  P0.09 (Arduino GPIO number 9)
   IO6       <->  P0.10 (Arduino GPIO number 10)
   IO7       <->  P0.28 (Arduino GPIO number 28)
   SW1       <->  P0.01 (Arduino GPIO number 1)
   A0        <->  P0.04/AIN2 (Arduino Analog A2
   A1        <->  P0.31/AIN7 (Arduino Analog A7
   SPI_CS    <->  P0.26 (Arduino GPIO number 26)
 */

// RAK4630 LoRa module
#define USE_SX1262
#define SX126X_CS (42)
#define SX126X_DIO1 (47)
#define SX126X_BUSY (46)
#define SX126X_RESET (38)
// #define SX126X_TXEN (39)
// #define SX126X_RXEN (37)
#define SX126X_POWER_EN (37)
// DIO2 controlls an antenna switch and the TCXO voltage is controlled by DIO3
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// enables 3.3V periphery like GPS or IO Module
#define PIN_3V3_EN (34)

// NO GPS
#undef GPS_RX_PIN
#undef GPS_TX_PIN

// RAK1910 GPS module
// If using the wisblock GPS module and pluged into Port A on WisBlock base
// IO1 is hooked to PPS (pin 12 on header) = gpio 17
// IO2 is hooked to GPS RESET = gpio 34, but it can not be used to this because IO2 is ALSO used to control 3V3_S power (1 is on).
// Therefore must be 1 to keep peripherals powered
// Power is on the controllable 3V3_S rail
// #define PIN_GPS_RESET (34)
// #define PIN_GPS_EN PIN_3V3_EN
// #define PIN_GPS_PPS (17) // Pulse per second input from the GPS

// #define GPS_RX_PIN PIN_SERIAL1_RX
// #define GPS_TX_PIN PIN_SERIAL1_TX

// RAK12002 RTC Module
#define RV3028_RTC (uint8_t)0b1010010

// Battery
// The battery sense is hooked to pin A0 (5)
// #define BATTERY_PIN PIN_A0
// and has 12 bit resolution
// #define BATTERY_SENSE_RESOLUTION_BITS 12
// #define BATTERY_SENSE_RESOLUTION 4096.0
// Definition of milliVolt per LSB => 3.0V ADC range and 12-bit ADC resolution = 3000mV/4096
// #define VBAT_MV_PER_LSB (0.73242188F)
// Voltage divider value => 1.5M + 1M voltage divider on VBAT = (1.5M / (1M + 1.5M))
// #define VBAT_DIVIDER (0.4F)
// Compensation factor for the VBAT divider
// #define VBAT_DIVIDER_COMP (1.73)
// Fixed calculation of milliVolt from compensation value
// #define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)
// #undef AREF_VOLTAGE
// #define AREF_VOLTAGE 3.0
// #define VBAT_AR_INTERNAL AR_INTERNAL_3_0
// #define ADC_MULTIPLIER VBAT_DIVIDER_COMP // REAL_VBAT_MV_PER_LSB
// #define VBAT_RAW_TO_SCALED(x) (REAL_VBAT_MV_PER_LSB * x)

// #define HAS_RTC 1

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif