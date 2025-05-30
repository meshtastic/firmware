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
// #include "nrf.h"
// #include "wiring_constants.h"
// #include "wiring_digital.h"



void initVariant()
{
    // LED1 & LED2
    pinMode(PIN_LED1, OUTPUT);
    digitalWrite(PIN_LED1, HIGH);
    //ledOff(PIN_LED1);

    pinMode(PIN_LED2, OUTPUT);
    digitalWrite(PIN_LED2, HIGH);
    //ledOff(PIN_LED2);

    // 3V3 Power Rail
    pinMode(PIN_3V3_EN, OUTPUT);
    digitalWrite(PIN_3V3_EN, HIGH);
}
