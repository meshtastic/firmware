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
#include "nrf_gpio.h"
#include "wiring_constants.h"
#include "wiring_digital.h"
#include <Wire.h>

// Linear 0..47 map: P0.00-P0.31 at indices 0-31, P1.00-P1.15 at indices 32-47.
// P0.00 and P0.01 are reserved for the 32 MHz crystal.
const uint32_t g_ADigitalPinMap[] = {
    // P0
    0xff, 0xff, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16,   17,   18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    // P1
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
};

void initVariant()
{
    // Bring up I2C peripheral rail (VCC_7A) — powers RTC and accelerometer
    pinMode(I2C_EN, OUTPUT);
    digitalWrite(I2C_EN, HIGH);

    // Bring up e-ink VCC rail
    pinMode(VCC_ELNK_EN, OUTPUT);
    digitalWrite(VCC_ELNK_EN, HIGH);

    // Enable GPS module
    pinMode(GPS_EN, OUTPUT);
    digitalWrite(GPS_EN, HIGH);

    // Connect battery voltage divider to ADC
    pinMode(ADC_EN, OUTPUT);
    digitalWrite(ADC_EN, HIGH);

    Wire.setPins(PIN_WIRE_SDA, PIN_WIRE_SCL);
}

void variant_shutdown()
{
    // Power down peripherals
    digitalWrite(I2C_EN, LOW);
    digitalWrite(VCC_ELNK_EN, LOW);
    digitalWrite(GPS_EN, LOW);
    digitalWrite(ADC_EN, LOW);

    // Drive all unused pins LOW to minimise leakage current
    for (int pin = 0; pin < 48; pin++) {
        if (pin == I2C_EN || pin == VCC_ELNK_EN || pin == GPS_EN || pin == ADC_EN ||
            pin == PIN_BUTTON1 ||
            pin == PIN_SPI_NSS  || pin == PIN_SPI_SCK  || pin == PIN_SPI_MOSI || pin == PIN_SPI_MISO ||
            pin == SX126X_DIO1  || pin == SX126X_RESET || pin == SX126X_BUSY) {
            continue;
        }
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
        if (pin >= 32)
            NRF_P1->DIRCLR = (1u << (pin - 32));
        else
            NRF_GPIO->DIRCLR = (1u << pin);
    }

    // Leave the primary button as a wake source
    nrf_gpio_cfg_input(PIN_BUTTON1, NRF_GPIO_PIN_PULLUP);
    nrf_gpio_cfg_sense_set(PIN_BUTTON1, NRF_GPIO_PIN_SENSE_LOW);
}
