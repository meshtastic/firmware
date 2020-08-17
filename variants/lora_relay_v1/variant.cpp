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

#include "variant.h"
#include "nrf.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

const uint32_t g_ADigitalPinMap[] = {
    // D0 .. D13
    25, // D0  is P0.25 (UART TX)
    24, // D1  is P0.24 (UART RX
    10, // D2  is P0.10 (NFC2)
    47, // D3  is P1.15 (LED1)
    42, // D4  is P1.10 (LED2)
    40, // D5  is P1.08
    7,  // D6  is P0.07
    34, // D7  is P1.02 (Button)
    16, // D8  is P0.16 (NeoPixel)
    26, // D9  is P0.26 D_RS (IPS data/command control)
    27, // D10 is P0.27
    6,  // D11 is P0.06 D_RES (IPS display reset)
    8,  // D12 is P0.08 D_CS (IPS display chip select)
    41, // D13 is P1.09 BLT (IPS display backlight)
    4,  // D14 is P0.04 SX1262 RXEN
    5,  // D15 is P0.05 BOOST_EN (5V buck converter enable for the the radio power)

    // D14 .. D21 (aka A0 .. A7)
    30, // D16 is P0.30 (A0)
    28, // D17 is P0.28 (A1)
    2,  // D18 is P0.02 (A2)
    3,  // D19 is P0.03 (A3)
    29, // D20 is P0.29 (A4, Battery)
    31, // D21 is P0.31 (A5, ARef)

    // D22 .. D23 (aka I2C pins)
    12, // D22 is P0.12 (SDA)
    11, // D23 is P0.11 (SCL)

    // D24 .. D26 (aka SPI pins)
    15, // D24 is P0.15 (SPI MISO)
    13, // D25 is P0.13 (SPI MOSI)
    14, // D26 is P0.14 (SPI SCK )

    // QSPI pins (not exposed via any header / test point)
    // 19, // P0.19 (QSPI CLK)
    // 20, // P0.20 (QSPI CS)
    // 17, // P0.17 (QSPI Data 0)
    // 22, // P0.22 (QSPI Data 1)
    // 23, // P0.23 (QSPI Data 2)
    // 21, // P0.21 (QSPI Data 3)

    // The remaining NFC pin
    9, // D27 P0.09 (NFC1, exposed only via test point on bottom of board)

    // The following pins were never listed as they were considered unusable
    // 0,  // P0.00 is XL1   (attached to 32.768kHz crystal) Never expose as GPIOs
    // 1,  // P0.01 is XL2   (attached to 32.768kHz crystal)
    18, // D28 P0.18 is RESET (attached to switch)
    // 32, // P1.00 is SWO   (attached to debug header)

    // D29-D43
    27,      // D29 P0.27 E22-SX1262 DIO1
    28,      // D30 P0.28 E22-SX1262 DIO2
    30,      // D31 P0.30 E22-SX1262 TXEN
    35,      // D32 P1.03 E22-SX1262 NSS
    32 + 8,  // D33 P1.08 E22-SX1262 BUSY
    32 + 12, // D34 P1.12 E22-SX1262 RESET
    32 + 1,  // P1.01 BTN_UP
    32 + 2,  // P1.02 SWITCH
    32 + 14, // D37 P1.14 is not connected per schematic
    36,      // P1.04 is not connected per schematic
    37,      // P1.05 is not connected per schematic
    38,      // P1.06 is not connected per schematic
    39,      // P1.07 is not connected per schematic
    43,      // P1.11 is not connected per schematic
    45,      // P1.13 is not connected per schematic
};

void initVariant()
{
    // LED1 & LED2
    pinMode(PIN_LED1, OUTPUT);
    ledOff(PIN_LED1);

    pinMode(PIN_LED2, OUTPUT);
    ledOff(PIN_LED2);
}
