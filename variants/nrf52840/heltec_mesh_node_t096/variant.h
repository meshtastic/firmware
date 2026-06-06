/*
 Copyright (c) 2014-2015 Arduino LLC.  All right reserved.
 Copyright (c) 2016 Sandeep Mistry All right reserved.
 Copyright (c) 2018, Adafruit Industries (adafruit.com)

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.
 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 See the GNU Lesser General Public License for more details.
 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

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

#define VEXT_ENABLE (0 + 26)
#define VEXT_ON_VALUE HIGH

// ST7735S TFT LCD
#define ST7735_CS (0 + 22)
#define ST7735_RS (0 + 15)  // DC
#define ST7735_SDA (0 + 17) // MOSI
#define ST7735_SCK (0 + 20)
#define ST7735_RESET (0 + 13)
#define ST7735_MISO -1
#define ST7735_BUSY -1
#define ST7735_BL (32 + 12)
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define SCREEN_ROTATE
#define TFT_HEIGHT 160
#define TFT_WIDTH 80
#define TFT_OFFSET_X 24
#define TFT_OFFSET_Y 0
#define TFT_INVERT false
#define SCREEN_TRANSITION_FRAMERATE 3 // fps
#define DISPLAY_FORCE_SMALL_FONTS

// Number of pins defined in PinDescription array
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

// LEDs
#define PIN_LED1 (0 + 28) // green (confirmed on 1.0 board)
#define LED_BLUE PIN_LED1 // fake for bluefruit library
#define LED_GREEN PIN_LED1
#define LED_STATE_ON 1 // State when LED is lit

// #define HAS_NEOPIXEL                         // Enable the use of neopixels
// #define NEOPIXEL_COUNT 2                     // How many neopixels are connected
// #define NEOPIXEL_DATA 14                     // gpio pin used to send data to the neopixels
// #define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800) // type of neopixels in use

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

/*
 * I2C
 */

#define WIRE_INTERFACES_COUNT 2

// I2C bus 0
#define PIN_WIRE_SDA (0 + 7) // SDA
#define PIN_WIRE_SCL (0 + 8) // SCL

// I2C bus 1
#define PIN_WIRE1_SDA (0 + 4)  // SDA (secondary bus)
#define PIN_WIRE1_SCL (0 + 27) // SCL (secondary bus)

/*
 * Lora radio
 */

#define USE_SX1262
#define SX126X_CS (0 + 5) // FIXME - we really should define LORA_CS instead
#define LORA_CS (0 + 5)
#define SX126X_DIO1 (0 + 21)
#define SX126X_BUSY (0 + 19)
#define SX126X_RESET (0 + 16)
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// ---- KCT8103L RF FRONT END CONFIGURATION ----
// The heltec_wireless_tracker_v2 uses a KCT8103L FEM chip with integrated PA and LNA
// RF path: SX1262 -> Pi attenuator -> KCT8103L PA -> Antenna
// Control logic (from KCT8103L datasheet):
//   Transmit PA:     CSD=1, CTX=1, CPS=1
//   Receive LNA:     CSD=1, CTX=0, CPS=X  (21dB gain, 1.9dB NF)
//   Receive bypass:  CSD=1, CTX=1, CPS=0
//   Shutdown:        CSD=0, CTX=X, CPS=X
// Pin mapping:
//   CPS (pin 5)  -> SX1262 DIO2: TX/RX path select (automatic via SX126X_DIO2_AS_RF_SWITCH)
//   CSD (pin 4)  -> GPIO12: Chip enable (HIGH=on, LOW=shutdown)
//   CTX (pin 6)  -> GPIO41: Switch between Receive LNA Mode and Receive Bypass Mode. (HIGH=RX bypass, LOW=RX LNA)
//   VCC0/VCC1    -> Vfem via U3 LDO, controlled by GPIO30
// KCT8103L FEM: TX/RX path switching is handled by DIO2 -> CPS pin (via SX126X_DIO2_AS_RF_SWITCH)

#define USE_KCT8103L_PA
#define LORA_PA_POWER (0 + 30)        // VFEM_Ctrl - KCT8103L LDO power enable
#define LORA_KCT8103L_PA_CSD (0 + 12) // CSD - KCT8103L chip enable (HIGH=on)
#define LORA_KCT8103L_PA_CTX                                                                                                     \
    (32 + 9) // CTX - Switch between Receive LNA Mode and Receive Bypass Mode. (HIGH=RX bypass, LOW=RX LNA)

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 2

// For LORA, spi 0
#define PIN_SPI_MISO (0 + 14)
#define PIN_SPI_MOSI (0 + 11)
#define PIN_SPI_SCK (32 + 8)

#define PIN_SPI1_MISO                                                                                                            \
    ST7735_MISO // FIXME not really needed, but for now the SPI code requires something to be defined, pick an used GPIO
#define PIN_SPI1_MOSI ST7735_SDA
#define PIN_SPI1_SCK ST7735_SCK

/*
 * GPS pins
 */
#define GPS_UC6580
#define GPS_BAUDRATE 115200
#define PIN_GPS_RESET (32 + 14) // An output to reset UC6580 GPS. As per datasheet, low for > 100ms will reset the UC6580
#define GPS_RESET_MODE LOW
#define PIN_GPS_EN (0 + 6)
#define GPS_EN_ACTIVE LOW
#define PERIPHERAL_WARMUP_MS 1000 // Make sure I2C QuickLink has stable power before continuing
#define PIN_GPS_PPS (32 + 11)
#define GPS_TX_PIN (0 + 25) // This is for bits going TOWARDS the CPU
#define GPS_RX_PIN (0 + 23) // This is for bits going TOWARDS the GPS

#define GPS_THREAD_INTERVAL 50

#define PIN_SERIAL1_RX GPS_RX_PIN
#define PIN_SERIAL1_TX GPS_TX_PIN

#define ADC_CTRL (32 + 15)
#define ADC_CTRL_ENABLED HIGH
#define BATTERY_PIN (0 + 3)
#define ADC_RESOLUTION 14

#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER (4.916F)

// rf52840 AIN1 = Pin 3
#define BATTERY_LPCOMP_INPUT NRF_LPCOMP_INPUT_1

// We have AIN1 with a VBAT divider so AIN1 = VBAT * (100/490)
// We have the device going deep sleep under 3.1V, which is AIN1 = 0.63V
// So we can wake up when VBAT>=VDD is restored to 3.3V, where AIN2 = 0.67V
// Ratio 0.67/3.3 = 0.20, so we can pick a bit higher, 2/8 VDD, which means
// VBAT=4.04V
#define BATTERY_LPCOMP_THRESHOLD NRF_LPCOMP_REF_SUPPLY_2_8

#define HAS_RTC 0
#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
