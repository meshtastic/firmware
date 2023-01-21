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
    25, // D0  SPI_MISO
    24, // D1  SPI_NSS
    23, // D2  SPI_SCK
    4,  // D3  VBAT
    11, // D4  DIO1
    27, // D5  BUSY
    19, // D6  NRESET
    12, // D7  BUTTON2
    22, // D8  BUTTON3
    26, // D9  SPI_MOSI
    31, // D10 UART_RX
    2,  // D11 UART_TX
    10, // D12 LED1 GREEN
    17, // D13 LED2 RED
    9,  // D14 BUZZER
    7,  // D15 BUTTON1
};

#include <initializer_list>
void initVariant()
{
    for (int i : {PIN_LED1, PIN_LED2}) {
        pinMode(i, OUTPUT);
        ledOff(i);
    }
}
