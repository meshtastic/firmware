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
#include "meshUtils.h"
#include "nrf.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

const uint32_t g_ADigitalPinMap[] = {
    // P0
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

    // P1
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47};

void initVariant()
{
    pinMode(KEY_POWER, OUTPUT);
    digitalWrite(KEY_POWER, HIGH);
    pinMode(RGB_POWER, OUTPUT);
    digitalWrite(RGB_POWER, HIGH);
    pinMode(green_LED_PIN, OUTPUT);
    digitalWrite(green_LED_PIN, LED_STATE_OFF);
    pinMode(LED_BLUE, OUTPUT);
    pinMode(PIN_POWER_USB, INPUT);
    pinMode(PIN_POWER_DONE, INPUT);
    pinMode(PIN_POWER_CHRG, INPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(EEPROM_POWER, OUTPUT);
    digitalWrite(EEPROM_POWER, HIGH);
    pinMode(PIN_EN1, OUTPUT);
    digitalWrite(PIN_EN1, HIGH);
    pinMode(PIN_EN2, OUTPUT);
    digitalWrite(PIN_EN2, HIGH);
    pinMode(ACC_POWER, OUTPUT);
    digitalWrite(ACC_POWER, LOW);
    pinMode(DHT_POWER, OUTPUT);
    digitalWrite(DHT_POWER, HIGH);
    pinMode(Battery_POWER, OUTPUT);
    digitalWrite(Battery_POWER, HIGH);
    pinMode(GPS_POWER, OUTPUT);
    digitalWrite(GPS_POWER, HIGH);
}

// called from main-nrf52.cpp during the cpuDeepSleep() function
void variant_shutdown()
{
    digitalWrite(red_LED_PIN, HIGH);
    digitalWrite(green_LED_PIN, HIGH);
    digitalWrite(LED_BLUE, HIGH);

    digitalWrite(PIN_EN1, LOW);
    digitalWrite(PIN_EN2, LOW);
    digitalWrite(EEPROM_POWER, LOW);
    digitalWrite(KEY_POWER, LOW);
    digitalWrite(DHT_POWER, LOW);
    digitalWrite(ACC_POWER, LOW);
    digitalWrite(Battery_POWER, LOW);
    digitalWrite(GPS_POWER, LOW);

    // This sets the pin to OUTPUT and LOW for the pins *not* in the if block.
    for (int pin = 0; pin < 48; pin++) {
        if (pin == PIN_POWER_USB || pin == BUTTON_PIN || pin == PIN_EN1 || pin == PIN_EN2 || pin == DHT_POWER ||
            pin == ACC_POWER || pin == Battery_POWER || pin == GPS_POWER || pin == LR1110_SPI_MISO_PIN ||
            pin == LR1110_SPI_MOSI_PIN || pin == LR1110_SPI_SCK_PIN || pin == LR1110_SPI_NSS_PIN || pin == LR1110_BUSY_PIN ||
            pin == LR1110_NRESET_PIN || pin == LR1110_IRQ_PIN || pin == GPS_TX_PIN || pin == GPS_RX_PIN || pin == green_LED_PIN ||
            pin == red_LED_PIN || pin == LED_BLUE) {
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

    nrf_gpio_cfg_input(BUTTON_PIN, NRF_GPIO_PIN_PULLUP); // Configure the pin to be woken up as an input
    nrf_gpio_pin_sense_t sense1 = NRF_GPIO_PIN_SENSE_LOW;
    nrf_gpio_cfg_sense_set(BUTTON_PIN, sense1);

    nrf_gpio_cfg_input(PIN_POWER_USB, NRF_GPIO_PIN_PULLDOWN); // Configure the pin to be woken up as an input
    nrf_gpio_pin_sense_t sense2 = NRF_GPIO_PIN_SENSE_HIGH;
    nrf_gpio_cfg_sense_set(PIN_POWER_USB, sense2);
}