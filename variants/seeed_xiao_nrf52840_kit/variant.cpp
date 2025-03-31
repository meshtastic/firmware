#include "variant.h"
#include "configuration.h"
#include "nrf.h"
#include "wiring_constants.h"
#include "wiring_digital.h"
#include <map>
#include <memory>
#include <stddef.h>
#include <stdint.h>
const uint32_t g_ADigitalPinMap[] = {
    // D0 .. D13
    2,  // D0  is P0.02 (A0)
    3,  // D1  is P0.03 (A1)
    28, // D2  is P0.28 (A2)
    29, // D3  is P0.29 (A3)
    4,  // D4  is P0.04 (A4,SDA)
    5,  // D5  is P0.05 (A5,SCL)
    43, // D6  is P1.11 (TX)
    44, // D7  is P1.12 (RX)
    45, // D8  is P1.13 (SCK)
    46, // D9  is P1.14 (MISO)
    47, // D10 is P1.15 (MOSI)

    // LEDs
    26, // D11 is P0.26 (LED RED)
    6,  // D12 is P0.06 (LED BLUE)
    30, // D13 is P0.30 (LED GREEN)
    14, // D14 is P0.14 (READ_BAT)

    // LSM6DS3TR
    40, // D15 is P1.08 (6D_PWR)
    27, // D16 is P0.27 (6D_I2C_SCL)
    7,  // D17 is P0.07 (6D_I2C_SDA)
    11, // D18 is P0.11 (6D_INT1)

    // MIC
    42, // 17,//42,  // D19 is P1.10 (MIC_PWR)
    32, // 26,//32,  // D20 is P1.00 (PDM_CLK)
    16, // 25,//16,  // D21 is P0.16 (PDM_DATA)

    // BQ25100
    13, // D22 is P0.13 (HICHG)
    17, // D23 is P0.17 (~CHG)

    //
    21, // D24 is P0.21 (QSPI_SCK)
    25, // D25 is P0.25 (QSPI_CSN)
    20, // D26 is P0.20 (QSPI_SIO_0 DI)
    24, // D27 is P0.24 (QSPI_SIO_1 DO)
    22, // D28 is P0.22 (QSPI_SIO_2 WP)
    23, // D29 is P0.23 (QSPI_SIO_3 HOLD)

    // NFC
    9,  // D30 is P0.09 (NFC1)
    10, // D31 is P0.10 (NFC2)

    // VBAT
    31, // D32 is P0.10 (VBAT)
};

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

void initVariant()
{
    // LED1 & LED2
    pinMode(21, OUTPUT);
    digitalWrite(21, LOW);
    // LED1 & LED2
    pinMode(22, OUTPUT);
    digitalWrite(22, LOW);

    pinMode(PIN_WIRE_SDA, INPUT_PULLUP);
    pinMode(PIN_WIRE_SCL, INPUT_PULLUP);
}