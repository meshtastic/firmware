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
#include "Arduino.h"
#include "nrf.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

const uint32_t g_ADigitalPinMap[] = {
    // P0 - pins 0 and 1 are hardwired for xtal and should never be enabled
    0xff, 0xff, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

    // P1
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47};

void initVariant()
{
    // No plain GPIO LEDs on this board (only WS2812 addressable LEDs, not driven here).
}

// Reproduces the vendor firmware's boot sequence from
// examples/original_test/original_test.ino. Runs before Meshtastic touches
// PIN_POWER_EN, so the RT9080 LDO gets a clean reset pulse and peripherals
// whose EN pins must be LOW at boot (GPS_EN, GPS_RF_EN, BUZZER) aren't left
// floating while the 3V3 rail is ramping.
void earlyInitVariant()
{
    // 3.3V rail: toggle RT9080_EN HIGH → LOW → HIGH with 100 ms dwell so the
    // LDO enters enable from a known state. The single-shot HIGH in main.cpp
    // is not enough on this hardware — if the chip was in a half-enabled
    // state from a previous reset, the rail brown-outs once LoRa TX fires.
    pinMode(PIN_POWER_EN, OUTPUT);
    digitalWrite(PIN_POWER_EN, HIGH);
    delay(100);
    digitalWrite(PIN_POWER_EN, LOW);
    delay(100);
    digitalWrite(PIN_POWER_EN, HIGH);
    delay(100);

    // Park peripherals with active-high enables LOW so they don't sink
    // current while the rest of setup() runs.
    pinMode(PIN_GPS_STANDBY, OUTPUT);
    digitalWrite(PIN_GPS_STANDBY, LOW);
    pinMode(PIN_GPS_RESET, OUTPUT);
    digitalWrite(PIN_GPS_RESET, LOW);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
}
