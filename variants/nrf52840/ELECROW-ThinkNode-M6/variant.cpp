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
    pinMode(LED_CHARGE, OUTPUT);
    ledOff(LED_CHARGE);

    pinMode(LED_PAIRING, OUTPUT);
    ledOff(LED_PAIRING);

    pinMode(VDD_FLASH_EN, OUTPUT);
    digitalWrite(VDD_FLASH_EN, HIGH);
}

// called from main-nrf52.cpp during the cpuDeepSleep() function
void variant_shutdown()
{
    // This sets the pin to OUTPUT and LOW for the pins *not* in the if block.
    for (int pin = 0; pin < 48; pin++) {
        if (pin == PIN_GPS_EN || pin == ADC_CTRL || pin == PIN_BUTTON1 || pin == PIN_SPI_MISO || pin == PIN_SPI_MOSI ||
            pin == PIN_SPI_SCK) {
            continue;
        }
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
        if (pin >= 32) {
            NRF_P1->DIRCLR = (1 << (pin - 32));
        } else {
            NRF_GPIO->DIRCLR = (1 << pin);
        }
    }

    digitalWrite(PIN_GPS_EN, LOW);
    digitalWrite(ADC_CTRL, LOW);
    // digitalWrite(RTC_POWER, LOW);

    nrf_gpio_cfg_input(PIN_BUTTON1, NRF_GPIO_PIN_PULLUP); // Configure the pin to be woken up as an input
    nrf_gpio_pin_sense_t sense1 = NRF_GPIO_PIN_SENSE_LOW;
    nrf_gpio_cfg_sense_set(PIN_BUTTON1, sense1);
}
