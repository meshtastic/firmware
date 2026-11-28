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
#define SPI_FREQUENCY 4000000
#define SCREEN_ROTATE
#define USE_POWER_BANK_GRAPHIC_PAGE 1 // laid out for this 220x128 panel
#define MAX_TFT_COLOR_REGIONS 96      // the power bank page alone draws about 60 regions

// Pins

#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

// No status LED is fitted
#define LED_STATE_ON 0

// Buttons (both have external 10K pull-ups, active LOW); primary and alternate as Heltec's firmware assigns them

#define PIN_BUTTON1 (32 + 11) // S2, silkscreened "RESET_SW" but wired to a GPIO, not nRESET
#define PIN_BUTTON2 (32 + 10) // S1 "USER_SW"

// I2C (P2 header to the power bank board: CW2217 fuel gauge)

#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA (0 + 13)
#define PIN_WIRE_SCL (0 + 5)

// CW2217 fuel gauge (0x64): the battery source, with the HLT331 below as fallback. Read only; its
// battery profile is loaded at the factory.
#define HAS_CW2217 1
// #define PIN_CW_INT (0 + 12)

// Power bank board (P1/P2 headers): the "9006" is an HLT331 power-bank controller. It streams status
// frames on its UART (Serial2, receive only) and raises IO1 on power events.
#define HAS_POWERBANK 1
#define HAS_HLT331 1
#define PIN_SERIAL2_RX (0 + 26)
#define PIN_SERIAL2_TX (0 + 15)
#define PIN_HLT331_IO1 (0 + 4)  // event pulse, active HIGH; wakes the board from System OFF
#define PIN_HLT331_IO2 (32 + 9) // "system working", active HIGH; not used yet

// IO_CSA drives Q3/Q2, which connect C2's VBUS (VBUSC22) to VDD_5V, the nRF52840's USB VBUS. Connected, USB
// enumerates but the bank cannot power a device on C2; HLT331PowerBank connects it only while C2 feeds the bank.
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
// No VBAT divider reaches the nRF52840 ADC. NRF_APM is left undefined so that external power and
// charging come from the HLT331, which sees both USB-C ports, rather than from the nRF's own VBUS.

#define PERIPHERAL_WARMUP_MS 1000
#define HAS_RTC 0 // No external RTC fitted

#ifdef __cplusplus
}
#endif

#endif
