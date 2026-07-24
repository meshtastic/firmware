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

// Evidence sources:
//   SCH: ThinkNode_M8_V0.3.sch (Eagle XML schematic, net labels)
//   PR:  meshtastic/firmware#9181 (Elecrow V0.1 firmware, hardware-tested)
//   REF: variants/nrf52840/ELECROW-ThinkNode-M1/variant.h (same MCU/radio/eink family)

#ifndef _VARIANT_ELECROW_THINKNODE_M8_
#define _VARIANT_ELECROW_THINKNODE_M8_

#define VARIANT_MCK (64000000ul)

#define USE_LFXO // Board uses 32.768 kHz crystal for LF (Y2/Y3 in schematic)

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

// NFC pins (not used as GPIO — reserved by nRF52840 NFC peripheral)
#define PIN_NFC1 (9)
#define PIN_NFC2 (10)

// Route SerialModule debug output to USB CDC (Serial), not Serial2 (unused on nRF52)
#define SERIAL_PRINT_PORT 0

// Enable canned-message and preset-message input
#define CANNED_MESSAGE_MODULE_ENABLE 1

/*
 * Power rails
 * I2C_EN   (P0.13) — enables I2C peripheral power (VCC_7A rail); active HIGH
 *                    Evidence: SCH net P0.13/VCC_7A_EN; PR #9181
 * VCC_ELNK_EN (P1.10) — enables e-ink VCC rail; active HIGH
 *                    Evidence: SCH net P1.10/VCC_EINK_EN; PR #9181
 * GPS_EN   (P0.16) — enables GPS module; active HIGH
 *                    Evidence: SCH net P0.16/GPS_EN; PR #9181
 * ADC_EN   (P1.08) — connects battery voltage divider to ADC pin; active HIGH
 *                    Evidence: SCH net P1.08/BADC_EN; PR #9181
 * USB_VBUS (P1.03) — senses USB VBUS presence
 *                    Evidence: SCH net P1.03/VBUS_T; PR #9181
 */
#define I2C_EN          (0 + 13)
#define VCC_ELNK_EN     (32 + 10)
#define GPS_EN          (0 + 16)
#define ADC_EN          (32 + 8)
#define USB_VBUS        (32 + 3)
#define EXT_PWR_DETECT  USB_VBUS

// Charge status (from LGS4056HDA charger IC; both active LOW)
// Evidence: SCH nets P1.05/CHRG, P1.06/DONE; PR #9181
#define CHRG (32 + 5)
#define DONE (32 + 6)

/*
 * Buttons — all active LOW with internal pull-up
 * Evidence: SCH nets P0.06/EC04_BUTTON, P0.08/EC04_A, P0.12/E_BUTTON, P1.09/EC04_B; PR #9181
 */
#define HAS_BUTTON              1
#define PIN_BUTTON1             (0 + 12)   // E_BUTTON (primary)
#define PIN_BUTTON_E            PIN_BUTTON1
#define PIN_BUTTON_EC04         (0 + 6)    // EC04 push-click
#define PIN_BUTTON_EC04_A       (0 + 8)    // EC04 encoder A
#define PIN_BUTTON_EC04_B       (32 + 9)   // EC04 encoder B

/*
 * LEDs
 * No dedicated status LED on this board (P0.14 is GPS 1PPS, not a free LED).
 * Evidence: SCH — no standalone LED GPIO; PR #9181
 */
#define PIN_LED1        -1
#define LED_BUILTIN     PIN_LED1
#define LED_BLUE        PIN_LED1
#define LED_STATE_ON    HIGH

/*
 * Buzzer
 * Evidence: SCH net P1.01/BUZZER via R41 1K; PR #9181
 */
#define PIN_BUZZER (32 + 1)

/*
 * I2C — single bus shared by PCF8563 RTC (0x51) and SC7A20HTR accelerometer (0x18/0x19)
 * Evidence: SCH nets P0.26/SDA, P0.27/SCL; PR #9181
 */
#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA (0 + 26)
#define PIN_WIRE_SCL (0 + 27)

// PCF8563 RTC on the shared I2C bus
// Evidence: SCH part U1 PCF8563 on SDA/SCL; PR #9181
#define PCF8563_RTC 0x51

/*
 * GPS — ATGM336H-5NR32 (multi-constellation: GPS/GLONASS/BeiDou/Galileo)
 *
 * Net-name convention in this schematic: "GPS_TX" = GPS module's TX output (→ MCU RX),
 *   "GPS_RX" = GPS module's RX input (← MCU TX).
 *
 * Evidence: SCH nets P1.02/GPS_TX→MCU-RX, P1.04/GPS_RX←MCU-TX,
 *   P0.15/GPS_ON/OFF (power toggle), P0.17/GPS_RST, P0.14/1PPS; PR #9181.
 *
 * TODO: verify — GPS_L76K macro used in PR #9181 but SCH clearly shows ATGM336H-5NR32.
 *   The ATGM336H driver (GNSS_MODEL_ATGM336H) is already supported in GPS.cpp.
 *   Remove GPS_L76K once GPS_ATGM336H (or no-macro auto-detect) is confirmed on HW.
 * TODO: verify — GPS_EN active level (PR uses HIGH; schematic resistor R58 direction unclear).
 * TODO: verify — baud rate (9600 is module default; confirm no custom NMEA config loaded).
 */
#define HAS_GPS 1
#define GPS_BAUDRATE 9600
#define GPS_TX_PIN      (32 + 2)   // P1.02 = GPS_TX net = MCU serial RX
#define GPS_RX_PIN      (32 + 4)   // P1.04 = GPS_RX net = MCU serial TX
#define PIN_SERIAL1_RX  GPS_TX_PIN
#define PIN_SERIAL1_TX  GPS_RX_PIN
#define PIN_GPS_RESET   (0 + 17)   // P0.17/GPS_RST — active LOW
#define PIN_GPS_STANDBY (0 + 15)   // P0.15/GPS_ON/OFF — power toggle
#define PIN_GPS_PPS     (0 + 14)   // P0.14/1PPS — pulse-per-second input
#define GPS_THREAD_INTERVAL 50

/*
 * QSPI external flash — MX25R1635F (16 Mbit)
 * Evidence: SCH nets P1.12-P1.15/QSPI_IO0-CS, P0.05/QSPI_IO3, P0.07/QSPI_IO2; PR #9181
 */
#define PIN_QSPI_SCK  (32 + 14)
#define PIN_QSPI_CS   (32 + 15)
#define PIN_QSPI_IO0  (32 + 12)   // MOSI / DI
#define PIN_QSPI_IO1  (32 + 13)   // MISO / DO
#define PIN_QSPI_IO2  (0 + 7)     // WP#
#define PIN_QSPI_IO3  (0 + 5)     // HOLD#/RESET#
#define EXTERNAL_FLASH_DEVICES MX25R1635F
#define EXTERNAL_FLASH_USE_QSPI

/*
 * SPI buses
 *   SPI0 — LoRa SX1262
 *   SPI1 — e-paper display
 * Evidence: SCH nets P0.19-P0.22/SX126X_S*, P0.29-P0.31/EINK_*; PR #9181
 */
#define SPI_INTERFACES_COUNT 2

// SPI0 (LoRa)
#define PIN_SPI_SCK  (0 + 19)
#define PIN_SPI_MOSI (0 + 20)
#define PIN_SPI_MISO (0 + 22)
#define PIN_SPI_NSS  (0 + 21)

// SPI1 (e-paper)
#define PIN_SPI1_SCK  (0 + 31)
#define PIN_SPI1_MOSI (0 + 29)
#define PIN_SPI1_MISO -1           // e-paper is write-only
#define PIN_SPI1_NSS  (0 + 30)

/*
 * E-paper display — CrowPanel Pico 2.4" (SPI, e-ink)
 *   Connected via 24-pin FFC connector (J1).
 *   Power: VCC_ELNK_EN (P1.10) gates VDD_EINK rail; PIN_EINK_EN (P1.11) is display-core enable.
 *
 * Evidence: SCH nets P0.28/EINK_DC, P0.30/EINK_CS, P0.02/EINK_RES,
 *   P0.03/EINK_BUSY, P0.29/EINK_MOSI, P0.31/EINK_SCLK, P1.10/VCC_EINK_EN, P1.11/EINK_EN;
 *   PR #9181; REF ELECROW-ThinkNode-M1 (identical pin assignments).
 *
 * TODO: verify — display controller and resolution.
 *   PR #9181 (Elecrow V0.1) carries GxEPD2_154_D67 / 200×200 — this appears to be a
 *   copy-paste from ThinkNode-M1 and is likely wrong for the 2.4" panel.
 *   Confirm the panel part number (Good Display GDEY024Y10 or similar 2.4" e-paper)
 *   and update EINK_DISPLAY_MODEL, EINK_WIDTH, EINK_HEIGHT accordingly.
 * TODO: verify — active level of PIN_EINK_EN (P1.11); assume HIGH to enable.
 */
#define USE_EINK 1
#define PIN_EINK_CS   PIN_SPI1_NSS   // P0.30
#define PIN_EINK_SCLK PIN_SPI1_SCK   // P0.31
#define PIN_EINK_MOSI PIN_SPI1_MOSI  // P0.29
#define PIN_EINK_DC   (0 + 28)
#define PIN_EINK_RES  (0 + 2)
#define PIN_EINK_BUSY (0 + 3)
#define PIN_EINK_EN   (32 + 11)       // display-core enable (P1.11/EINK_EN)

/*
 * LoRa radio — SX1262
 *   DIO3 (P0.23) drives the on-board NT2016SA-32M TCXO; not connected back as GPIO input.
 *   DIO2 drives the PE4259 RF switch (TX/RX); handled internally via DIO2_AS_RF_SWITCH.
 *   BUSY is the read-back signal from SX1262 on P1.00.
 *
 * Evidence: SCH nets P0.19-P0.25/SX126X_*, P1.00/SX126X_BUSY, P0.23/SX126X_DIO3; PR #9181
 */
#define USE_SX1262
#define SX126X_CS           PIN_SPI_NSS   // P0.21
#define SX126X_RESET        (0 + 24)      // P0.24/SX126X_RESET
#define SX126X_DIO1         (0 + 25)      // P0.25/SX126X_DIO1 (IRQ)
#define SX126X_BUSY         (32 + 0)      // P1.00/SX126X_BUSY
#define SX126X_DIO2_AS_RF_SWITCH          // DIO2→PE4259 RF switch (not routed to MCU GPIO)
#define SX126X_DIO3_TCXO_VOLTAGE 3.3     // DIO3 powers NT2016SA-32M TCXO at 3.3 V

/*
 * Battery ADC
 *   P0.04 reads through a voltage divider (R4 top, R6 bottom).
 *   ADC_EN (P1.08) must be driven HIGH before sampling.
 *   ADC_MULTIPLIER 1.75 is the hardware-calibrated value from PR #9181 (tested on device).
 *
 * TODO: verify — ADC_MULTIPLIER.  Schematic shows equal-value resistors implying 2.0×
 *   with 2.4 V reference; PR #9181 uses 1.75 (hardware-measured).  Confirm resistor
 *   values (R4, R6) and calibrate against a known voltage before shipping.
 */
#define BATTERY_PIN (0 + 4)
#define PIN_A0      BATTERY_PIN
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#define BATTERY_SENSE_SAMPLES 100
#undef  AREF_VOLTAGE
#define AREF_VOLTAGE 2.4
#define VBAT_AR_INTERNAL AR_INTERNAL_2_4
#define ADC_MULTIPLIER (1.75F)

#ifdef __cplusplus
}
#endif

#endif
