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
#include "configuration.h"
#include "nrf.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

const uint32_t g_ADigitalPinMap[] = {
    // P0 - pins 0 and 1 are hardwired for xtal and should never be enabled
    0xff, 0xff, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

    // P1
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47};

int m1HasAg3352 = 0;

void initVariant()
{
    // pinMode(PIN_LED1, OUTPUT);
    // ledOff(PIN_LED1);
}

// Here rather than initVariant(), whose delay() would run before the scheduler. No LOG_* yet.
void earlyInitVariant()
{
    pinMode(PIN_GPS_VERSION, INPUT);
    delay(2);
    uint8_t high = 0;
    for (uint8_t i = 0; i < 10; i++)
        high += digitalRead(PIN_GPS_VERSION) == HIGH;
    m1HasAg3352 = high >= 3;

#if !MESHTASTIC_EXCLUDE_GPS
    if (m1HasAg3352) {
        pinMode(GPS_SLEEP_INT, OUTPUT);
        digitalWrite(GPS_SLEEP_INT, HIGH);
        // The AG3352 stays powered across an MCU reset and may still be in backup mode; only RESETB ends that.
        pinMode(PIN_GPS_RESET, OUTPUT);
        digitalWrite(PIN_GPS_RESET, HIGH); // asserts RESETB through Q16
        delay(10);
        digitalWrite(PIN_GPS_RESET, LOW);
    }
#endif
}

void lateInitVariant()
{
    LOG_INFO("ThinkNode M1 GPS %s", m1HasAg3352 ? "AG3352" : "L76K");
}

void variant_shutdown()
{
#if !MESHTASTIC_EXCLUDE_GPS
    if (m1HasAg3352) {
        digitalWrite(GPS_SLEEP_INT, HIGH); // idle_int must stay high through System OFF or the GPS never idles
        pinMode(PIN_GPS_RESET, OUTPUT);
        digitalWrite(PIN_GPS_RESET, LOW); // releases RESETB through Q16; a floating base could hold it asserted
    }
#endif
    for (int pin = 0; pin < 48; pin++) {
        if (pin == SX126X_BUSY || pin == PIN_SPI_SCK || pin == SX126X_DIO1 || pin == PIN_SPI_MOSI || pin == PIN_SPI_MISO ||
            pin == SX126X_CS || pin == SX126X_RESET || pin == PIN_NFC1 || pin == PIN_NFC2 || pin == PIN_BUTTON1 ||
            pin == PIN_BUTTON2) {
            continue;
        }
#if !MESHTASTIC_EXCLUDE_GPS
        if (m1HasAg3352 && (pin == GPS_SLEEP_INT || pin == PIN_GPS_RESET))
            continue;
#endif
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
        if (pin >= 32) {
            NRF_P1->DIRCLR = (1 << (pin - 32));
        } else {
            NRF_GPIO->DIRCLR = (1 << pin);
        }
    }
    nrf_gpio_cfg_input(PIN_BUTTON1, NRF_GPIO_PIN_PULLUP); // Configure the pin to be woken up as an input
    nrf_gpio_pin_sense_t sense = NRF_GPIO_PIN_SENSE_LOW;
    nrf_gpio_cfg_sense_set(PIN_BUTTON1, sense);

    nrf_gpio_cfg_input(PIN_BUTTON2, NRF_GPIO_PIN_PULLUP);
    nrf_gpio_pin_sense_t sense1 = NRF_GPIO_PIN_SENSE_LOW;
    nrf_gpio_cfg_sense_set(PIN_BUTTON2, sense1);
}