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

#ifndef _VARIANT_HELTEC_MESH_POCKET_V2_
#define _VARIANT_HELTEC_MESH_POCKET_V2_

#define VARIANT_MCK (64000000ul)
#define USE_LFXO // Board uses 32khz crystal for LF

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#ifndef HELTEC_MESH_POCKET_V2
#define HELTEC_MESH_POCKET_V2
#endif

// Display (1.08" NV3001B 128x220 TFT, SPI1)
#define HAS_SCREEN 1
#define HAS_SPI_TFT 1
#define USE_TFTDISPLAY 1
#define USE_ARDUINO_GFX 1
#define TFT_NV3001B 1
#define TFT_SCL (32 + 1)
#define TFT_SDA (32 + 7)
#define TFT_CS (0 + 16)
#define TFT_RS (32 + 0)
#define TFT_DC TFT_RS
#define TFT_RST (0 + 24)
#define VTFT_CTRL (0 + 9) // VScreen_Ctrl, P-FET on the panel rail
#define TFT_EN_ON LOW
#define TFT_EN_OFF HIGH
#define TFT_BL (0 + 25) // LED_K, N-FET on the backlight cathode
#define TFT_BACKLIGHT_ON HIGH
#define TFT_BACKLIGHT_OFF LOW
#define TFT_WIDTH 128
#define TFT_HEIGHT 220
#define SPI_FREQUENCY 8000000
#define SCREEN_ROTATE

// Pins

#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

// No status LED is fitted
#define LED_STATE_ON 0

// Buttons (both have external 10K pull-ups, active LOW)

#define PIN_BUTTON1 (32 + 10) // S1 "USER_SW"
#define PIN_BUTTON2 (32 + 11) // S2, silkscreened "RESET_SW" but wired to a GPIO, not nRESET

// Serial (P4 debug header)

#define PIN_SERIAL2_RX (0 + 7)
#define PIN_SERIAL2_TX (0 + 8)

// I2C (P2 header to the power bank board: CW2015 fuel gauge)

#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA (0 + 13)
#define PIN_WIRE_SCL (0 + 5)

#define HAS_CW2015 1
// #define CW2015_INT_PIN (0 + 12) // CW_INT - populated, not yet used in firmware

// Power bank board (P1/P2 headers)
// "9006" UART and GPIOs to the power bank controller - wired, not yet used in firmware
// #define PIN_9006_RX (0 + 15)
// #define PIN_9006_TX (0 + 26)
// #define PIN_9006_IO1 (0 + 4)
// #define PIN_9006_IO2 (32 + 9)

// IO_CSA switches VBUSC2 from the power bank board through to the nRF52840 VBUS pin; without it USB does not enumerate.
#define PIN_USB_VBUS_EN (0 + 20)
#define PIN_USB_VBUS_EN_ACTIVE HIGH

// LoRa (SX1262)

#define USE_SX1262
#define SX126X_CS (0 + 14) // FIXME - we really should define LORA_CS instead
#define LORA_CS SX126X_CS
#define SX126X_DIO1 (0 + 31)
#define SX126X_BUSY (0 + 29)
#define SX126X_RESET (0 + 11)
#define SX126X_DIO2_AS_RF_SWITCH // DIO2 drives the AW13102 antenna switch through Q6
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// SPI

#define SPI_INTERFACES_COUNT 2

// SPI0 - LoRa
#define PIN_SPI_MISO (0 + 2)
#define PIN_SPI_MOSI (32 + 15)
#define PIN_SPI_SCK (32 + 13)

// SPI1 - Display (write-only)
#define PIN_SPI1_MISO (-1)
#define PIN_SPI1_MOSI TFT_SDA
#define PIN_SPI1_SCK TFT_SCL

// GPS (UC6580)

#define GPS_UC6580
#define GPS_BAUDRATE 115200
#define PIN_GPS_RESET (32 + 6)
#define GPS_RESET_MODE LOW
#define PIN_GPS_EN (0 + 22) // VGNSS_Ctrl, P-FET on VDD_GNSS: LOW = powered
#define GPS_EN_ACTIVE LOW
#define PIN_GPS_PPS (0 + 10)
#define GPS_TX_PIN (32 + 4) // GNSS_RX, towards the GPS
#define GPS_RX_PIN (32 + 2) // GNSS_TX, towards the CPU
#define GPS_THREAD_INTERVAL 50

#define PIN_SERIAL1_RX GPS_RX_PIN
#define PIN_SERIAL1_TX GPS_TX_PIN

// Power / battery
// No VBAT divider reaches the nRF52840 ADC; battery state comes from the CW2015 over I2C.

#define NRF_APM   // USB VBUS detection via nrfx_power_usbstatus_get()
#define HAS_RTC 0 // No external RTC fitted

#ifdef __cplusplus
}
#endif

#endif
