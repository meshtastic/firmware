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

#ifndef _VARIANT_ELECROW_EINK_V1_0_
#define _VARIANT_ELECROW_EINK_V1_0_

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
// 在PinDescription数组中定义的引脚数
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

#define PIN_LED1 -1
#define PIN_LED2 -1
#define PIN_LED3 -1

// LED
#define POWER_LED (32 + 6) // red
#define LED_POWER (32 + 4)
#define USER_LED (0 + 13) // green
// USB_CHECK
#define USB_CHECK (32 + 3)
#define ADC_V (0 + 8)

#define LED_RED PIN_LED3
#define LED_BLUE PIN_LED1
#define LED_GREEN PIN_LED2
#define LED_BUILTIN LED_BLUE
#define LED_CONN PIN_GREEN
#define LED_STATE_ON 0 // State when LED is lit  // LED灯亮时的状态
#define M1_buzzer (0 + 6)
/*
 * Buttons
 */
#define PIN_BUTTON2 (32 + 10)
#define PIN_BUTTON1 (32 + 7)

// #define PIN_BUTTON1 (0 + 11)
// #define PIN_BUTTON1 (32 + 7)

// #define BUTTON_CLICK_MS 400
// #define BUTTON_TOUCH_MS 200

/*
 * Analog pins
 */
#define PIN_A0 (4) // Battery ADC

#define BATTERY_PIN PIN_A0

static const uint8_t A0 = PIN_A0;

#define ADC_RESOLUTION 14

#define PIN_NFC1 (9)
#define PIN_NFC2 (10)

/*Wire Interfaces*/
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (26)
#define PIN_WIRE_SCL (27)

/* touch sensor, active high */

#define TP_SER_IO (0 + 11)

#define PIN_RTC_INT (0 + 16) // Interrupt from the PCF8563 RTC

/*
External serial flash WP25R1635FZUIL0
*/

// QSPI Pins
#define PIN_QSPI_SCK (32 + 14)
#define PIN_QSPI_CS (32 + 15)
#define PIN_QSPI_IO0 (32 + 12) // MOSI if using two bit interface
#define PIN_QSPI_IO1 (32 + 13) // MISO if using two bit interface
#define PIN_QSPI_IO2 (0 + 7)   // WP if using two bit interface (i.e. not used)
#define PIN_QSPI_IO3 (0 + 5)   // HOLD if using two bit interface (i.e. not used)

// On-board QSPI Flash
#define EXTERNAL_FLASH_DEVICES MX25R1635F
#define EXTERNAL_FLASH_USE_QSPI

/*
 * Lora radio
 */
#define SX126X_POWER_EN (0 + 21)
#define USE_SX1262
#define SX126X_CS (0 + 24) // FIXME - we really should define LORA_CS instead
#define SX126X_DIO1 (0 + 20)
// Note DIO2 is attached internally to the module to an analog switch for TX/RX switching
// #define SX1262_DIO3 (0 + 21) // This is used as an *output* from the sx1262 and connected internally to power the tcxo, do not
// drive from the main
#define SX126X_BUSY (0 + 17)
#define SX126X_RESET (0 + 25)
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 3.3

#define PIN_EINK_EN (32 + 11) // Note: this is really just backlight power
#define PIN_EINK_CS (0 + 30)
#define PIN_EINK_BUSY (0 + 3)
#define PIN_EINK_DC (0 + 28)
#define PIN_EINK_RES (0 + 2)
#define PIN_EINK_SCLK (0 + 31)
#define PIN_EINK_MOSI (0 + 29) // also called SDI

// Controls power for all peripherals (eink + GPS + LoRa + Sensor)
#define PIN_POWER_EN (0 + 12)

#define USE_EINK

#define PIN_SPI1_MISO (32 + 7)
#define PIN_SPI1_MOSI PIN_EINK_MOSI
#define PIN_SPI1_SCK PIN_EINK_SCLK

/*
 * GPS pins
 */
// #define HAS_GPS 1
#define GPS_L76K
#define GPS_BAUDRATE 9600
#define PIN_GPS_REINIT (32 + 5)  // An output to reset L76K GPS. As per datasheet, low for > 100ms will reset the L76K
#define PIN_GPS_STANDBY (32 + 2) // An output to wake GPS, low means allow sleep, high means force wake
// Seems to be missing on this new board
// #define PIN_GPS_PPS (32 + 4)  // Pulse per second input from the GPS
#define GPS_TX_PIN (32 + 9) // This is for bits going TOWARDS the CPU
#define GPS_RX_PIN (32 + 8) // This is for bits going TOWARDS the GPS

#define GPS_THREAD_INTERVAL 50

#define PIN_GPS_PPS (32 + 1) // GPS开关判断

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

#define PIN_PWR_EN (0 + 6)

// To debug via the segger JLINK console rather than the CDC-ACM serial device
// #define USE_SEGGER

// Battery
// The battery sense is hooked to pin A0 (4)
// it is defined in the anlaolgue pin section of this file
// and has 12 bit resolution
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER (2.02F)

// #define HAS_RTC 0
// #define HAS_SCREEN 0

#ifdef __cplusplus
}
#endif

#endif