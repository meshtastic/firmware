/*
A DIY, easy, no PCB, basic but powerful node based on E22 900M33S and
Pro Micro compatible NRF52840 dev boards such as the Tenstar Robot
Supermini or Nice Nano.

Without adding GPS or other peripherals, this node should run about 45
hours on a 1S 1000 mAh battery.

This node can be constructed by directly soldering the development
board directly to an EBYTE E22 900m30S.  There will be 7 directly
soldered pads, 3 header pins, and only 1 wire connection required.

You can power the node via USB or a battery, or you can install a power
connector on the GND and VCC pads of the E22; a JST PH or XH socket
should fit.  It may be easiest to install this first if you want it.

Note that the E22-900M33S will output near it's specified power level
only when supplied with 5-5.5 volts.  If you take advantage of the
battery charger on the Pro Micro board and power everything from a 1S
Lithium battery, the E22-900M33S will output a maximum of about 1
Watt.

Be sure to perform the initial flash of the Meshtastic firmware before
proceeding with the assembly to protect the E22 and ensure that the
TXEN pad is not driven when it shouldn't be.

Detailed build instructions can be found here:
https://github.com/brad112358/easy_E22?tab=readme-ov-file#easy-diy-e22-nrf52840-build-instructions

*/

#ifndef _VARIANT_EASY_PROMICRO_DIY_
#define _VARIANT_EASY_PROMICRO_DIY_

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

// #define USE_LFXO // Board uses 32khz crystal for LF
#define USE_LFRC // Board uses RC for LF

#define PROMICRO_DIY_TCXO

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/*
E22/NRF52 PRO MICRO PIN ASSIGNMENT

| Pin   | Function    |     | Pin      | Function     |
| ----- | ----------- | --- | -------- | ------------ |
| Gnd   |             |     | vbat     |              |
| P0.06 | Serial2 RX  |     | vbat     |              |
| P0.08 | Serial2 TX  |     | Gnd      |              |
| Gnd   |             |     | reset    |              |
| Gnd   | GND         |     | ext_vcc  | *see 0.13    |
| P0.17 | Free pin    |     | P0.31    | BATTERY_PIN  |
| P0.20 | Free pin    |     | P0.29    | DI01         |
| P0.22 | Free pin    |     | P0.02    | BUSY         |
| P0.24 | Free pin    |     | P1.15    | NRST         |
| P1.00 | TXEN        |     | P1.13    | MISO         |
| P0.11 | RXEN        |     | P1.11    | MOSI         |
| P1.04 | SDA         |     | P0.10    | SCK          |
| P1.06 | SCL         |     | P0.09    | NSS          |
|       |             |     |          |              |
|       | Mid board   |     |          | Internal     |
| P1.01 | Free pin    |     | 0.15     | LED          |
| P1.02 | GPS_RX      |     | 0.13     | 3V3_EN       |
| P1.07 | GPS_TX      |     |          |              |
*/

// Number of pins defined in PinDescription array
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

// Pin 13 enables 3.3V periphery.
// #define PIN_3V3_EN (0 + 13) // P0.13

// Battery
#define BATTERY_PIN (0 + 31) // P0.31
#define ADC_CHANNEL ADC1_GPIO4_CHANNEL
#define ADC_RESOLUTION 14
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
// Definition of milliVolt per LSB => 3.0V ADC range and 12-bit ADC resolution = 3000mV/4096
#define VBAT_MV_PER_LSB (0.73242188F)
// Voltage divider value => 1.5M + 1M voltage divider on VBAT = (1.5M / (1M + 1.5M))
#define VBAT_DIVIDER (0.6F)
// Compensation factor for the VBAT divider
#define VBAT_DIVIDER_COMP (2.02) // 2 + magic for current drain of input
// Fixed calculation of milliVolt from compensation value
#define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER VBAT_DIVIDER_COMP // REAL_VBAT_MV_PER_LSB
#define VBAT_RAW_TO_SCALED(x) (REAL_VBAT_MV_PER_LSB * x)

// WIRE IC AND IIC PINS
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (32 + 4) // P1.04
#define PIN_WIRE_SCL (32 + 6) // P1.06

// LED
#define PIN_LED1 (0 + 15) // P0.15
#define LED_BUILTIN PIN_LED1
// Actually red
#define LED_BLUE PIN_LED1
#define LED_STATE_ON 1 // State when LED is lit

// Button
#define BUTTON_PIN (-1) // no button

// GPS
#define PIN_GPS_TX (32 + 7) // P1.07
#define PIN_GPS_RX (32 + 2) // P1.02

#define PIN_GPS_EN (0 + 13) // P0.13 3.3V enable
#define GPS_POWER_TOGGLE
#define GPS_UBLOX
// define GPS_DEBUG

// UART interfaces
#define PIN_SERIAL1_RX PIN_GPS_TX
#define PIN_SERIAL1_TX PIN_GPS_RX

#define PIN_SERIAL2_RX (0 + 6) // P0.06
#define PIN_SERIAL2_TX (0 + 8) // P0.08

// Serial interfaces
#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO (32 + 13) // P1.13
#define PIN_SPI_MOSI (32 + 11) // P1.11
#define PIN_SPI_SCK (0 + 10)   // P0.10

#define LORA_MISO PIN_SPI_MISO
#define LORA_MOSI PIN_SPI_MOSI
#define LORA_SCK PIN_SPI_SCK
#define LORA_CS (0 + 9) // P0.09 NSS

// LORA MODULE
#define USE_SX1262

#define LORA_DIO0 (0 + 2)    // P0.02 BUSY
#define LORA_DIO1 (0 + 29)   // P0.29 IRQ
#define LORA_RESET (32 + 15) // P1.15 NRST

// SX126X CONFIG
#undef TX_GAIN_LORA
#define TX_GAIN_LORA 22    // 8 for E22 900M30S, 25 for 900M33S, 22 for 3.7V battery powered 900M33S,  0 for 900M22S
#define SX126X_MAX_POWER 8 // 8 for 900M33S, 22 for 900M30S and 900M22S; defaults to 22 if not defined

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO0
#define SX126X_RESET LORA_RESET
#define SX126X_RXEN (0 + 11) // P0.11
#define SX126X_TXEN (32 + 0) // P1.00

/*
| Mfr          | Module           | TCXO | RF Switch | Notes                                 |
| ------------ | ---------------- | ---- | --------- | ------------------------------------- |
| Ebyte        | E22-900M22S      | Yes  | Ext       |                                       |
| Ebyte        | E22-900M30S      | Yes  | Ext       |                                       |
| Ebyte        | E22-900M33S      | Yes  | Ext       | MAX_POWER must be set to 8 for this!  |

On the SX1262, DIO3 sets the voltage for an external TCXO, if one is present. If one is not present, use TCXO_OPTIONAL to try both
settings.
*/

#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define TCXO_OPTIONAL // make it so that the firmware can try both TCXO and XTAL

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
