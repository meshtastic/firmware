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
    // P0 - pins 0 and 1 are hardwired for xtal and should never be enabled
    0xff, 0xff, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

    // P1
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47};

void initVariant()
{
    // LED1 & LED2
    pinMode(PIN_LED3, OUTPUT);
    digitalWrite(PIN_LED3, !LED_STATE_ON);

    pinMode(PIN_LED2, OUTPUT);
    digitalWrite(PIN_LED2, !LED_STATE_ON);

    // pinMode(PIN_LED1, OUTPUT);
    // digitalWrite(PIN_LED1, !LED_STATE_ON);

    pinMode(PIN_QSPI_CS, OUTPUT);
    digitalWrite(PIN_QSPI_CS, HIGH);

#if defined(PIN_EXT_FLASH_CS)
    pinMode(PIN_EXT_FLASH_SCK, OUTPUT);
    digitalWrite(PIN_EXT_FLASH_SCK, LOW);
    pinMode(PIN_EXT_FLASH_MOSI, OUTPUT);
    digitalWrite(PIN_EXT_FLASH_MOSI, LOW);
    pinMode(PIN_EXT_FLASH_MISO, INPUT);
    pinMode(PIN_EXT_FLASH_CS, OUTPUT);
    digitalWrite(PIN_EXT_FLASH_CS, HIGH);
    pinMode(PIN_EXT_FLASH_WP, OUTPUT);
    digitalWrite(PIN_EXT_FLASH_WP, HIGH);
    pinMode(PIN_EXT_FLASH_HOLD, OUTPUT);
    digitalWrite(PIN_EXT_FLASH_HOLD, HIGH);
#endif
}

void variant_shutdown()
{
    // Turn off all LEDs before deep sleep.
    digitalWrite(PIN_LED3, !LED_STATE_ON);
    digitalWrite(PIN_LED2, !LED_STATE_ON);
    // digitalWrite(PIN_LED1, !LED_STATE_ON);

#if defined(PIN_EINK_POWER)
    pinMode(PIN_EINK_DC, INPUT);
    pinMode(PIN_EINK_RES, INPUT);
    pinMode(PIN_EINK_CS, INPUT);
    pinMode(PIN_EINK_MOSI, INPUT);
    pinMode(PIN_EINK_SCLK, INPUT);
    pinMode(PIN_EINK_BUSY, INPUT_PULLDOWN);
    // Cut panel LDO last (active-LOW: !EINK_POWER_ACTIVE = OFF)
    digitalWrite(PIN_EINK_POWER, !EINK_POWER_ACTIVE);
#endif

#if defined(USE_SX1262) && defined(PIN_LORA_PWR)
    pinMode(PIN_LORA_RST, INPUT);
    pinMode(PIN_LORA_CS, INPUT);
    pinMode(PIN_LORA_MOSI, INPUT);
    pinMode(PIN_LORA_MISO, INPUT);
    pinMode(PIN_LORA_SCK, INPUT);
    pinMode(PIN_LORA_DIO1, INPUT_PULLDOWN);
    pinMode(PIN_LORA_BUSY, INPUT_PULLDOWN);
    digitalWrite(PIN_LORA_PWR, LOW);
#endif

#if defined(PIN_EXT_FLASH_CS)
    pinMode(PIN_EXT_FLASH_SCK, OUTPUT);
    digitalWrite(PIN_EXT_FLASH_SCK, LOW);
    pinMode(PIN_EXT_FLASH_MOSI, OUTPUT);
    digitalWrite(PIN_EXT_FLASH_MOSI, LOW);
    pinMode(PIN_EXT_FLASH_MISO, INPUT);
    pinMode(PIN_EXT_FLASH_CS, OUTPUT);
    digitalWrite(PIN_EXT_FLASH_CS, HIGH);
    pinMode(PIN_EXT_FLASH_WP, OUTPUT);
    digitalWrite(PIN_EXT_FLASH_WP, HIGH);
    pinMode(PIN_EXT_FLASH_HOLD, OUTPUT);
    digitalWrite(PIN_EXT_FLASH_HOLD, HIGH);
#endif
}