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
    pinMode(PIN_USB_VBUS_EN, OUTPUT);
    digitalWrite(PIN_USB_VBUS_EN, PIN_USB_VBUS_EN_ACTIVE);
}

void variant_shutdown()
{
    nrf_gpio_cfg_default(TFT_CS);
    nrf_gpio_cfg_default(TFT_RS);
    nrf_gpio_cfg_default(TFT_SDA);
    nrf_gpio_cfg_default(TFT_SCL);
    nrf_gpio_cfg_default(TFT_RST);
    nrf_gpio_cfg_default(TFT_BL);    // R10 pulls the backlight gate low
    nrf_gpio_cfg_default(VTFT_CTRL); // R1 pulls the rail switch off

    nrf_gpio_cfg_default(PIN_WIRE_SDA);
    nrf_gpio_cfg_default(PIN_WIRE_SCL);

    nrf_gpio_cfg_default(SX126X_CS);
    nrf_gpio_cfg_default(SX126X_DIO1);
    nrf_gpio_cfg_default(SX126X_BUSY);
    nrf_gpio_cfg_default(SX126X_RESET);

    nrf_gpio_cfg_default(PIN_SPI_MISO);
    nrf_gpio_cfg_default(PIN_SPI_MOSI);
    nrf_gpio_cfg_default(PIN_SPI_SCK);

    nrf_gpio_cfg_default(PIN_SPI1_MOSI);
    nrf_gpio_cfg_default(PIN_SPI1_SCK);

    nrf_gpio_cfg_default(PIN_GPS_RESET);
    nrf_gpio_cfg_default(PIN_GPS_EN); // R11 pulls the GNSS rail switch off
    nrf_gpio_cfg_default(PIN_GPS_PPS);
    nrf_gpio_cfg_default(GPS_TX_PIN);
    nrf_gpio_cfg_default(GPS_RX_PIN);

    Serial2.end();
    nrf_gpio_cfg_default(PIN_SERIAL2_RX);
    nrf_gpio_cfg_default(PIN_SERIAL2_TX);

    // Wake from System OFF on the HLT331's next power event. Sense the level IO1 is not at now,
    // so a line already held high does not wake the board straight back up.
    detachInterrupt(PIN_HLT331_IO1);
    pinMode(PIN_HLT331_IO1, INPUT_PULLDOWN);
    const nrf_gpio_pin_sense_t sense = digitalRead(PIN_HLT331_IO1) ? NRF_GPIO_PIN_SENSE_LOW : NRF_GPIO_PIN_SENSE_HIGH;
    nrf_gpio_cfg_sense_input(PIN_HLT331_IO1, NRF_GPIO_PIN_PULLDOWN, sense);
}
