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
    // LED1
    pinMode(PIN_LED1, OUTPUT);
    ledOff(PIN_LED1);
}

void variant_shutdown()
{
    nrf_gpio_cfg_default(VEXT_ENABLE);
    nrf_gpio_cfg_default(ST7735_CS);
    nrf_gpio_cfg_default(ST7735_RS);
    nrf_gpio_cfg_default(ST7735_SDA);
    nrf_gpio_cfg_default(ST7735_SCK);
    nrf_gpio_cfg_default(ST7735_RESET);
    nrf_gpio_cfg_default(ST7735_BL);

    nrf_gpio_cfg_default(PIN_LED1);

    // nrf_gpio_cfg_default(LORA_PA_POWER);
    pinMode(LORA_PA_POWER, OUTPUT);
    digitalWrite(LORA_PA_POWER, LOW);

    nrf_gpio_cfg_default(LORA_KCT8103L_PA_CSD);
    nrf_gpio_cfg_default(LORA_KCT8103L_PA_CTX);

    pinMode(ADC_CTRL, OUTPUT);
    digitalWrite(ADC_CTRL, LOW);

    nrf_gpio_cfg_default(SX126X_CS);
    nrf_gpio_cfg_default(SX126X_DIO1);
    nrf_gpio_cfg_default(SX126X_BUSY);
    nrf_gpio_cfg_default(SX126X_RESET);

    nrf_gpio_cfg_default(PIN_SPI_MISO);
    nrf_gpio_cfg_default(PIN_SPI_MOSI);
    nrf_gpio_cfg_default(PIN_SPI_SCK);

    nrf_gpio_cfg_default(PIN_GPS_PPS);
    nrf_gpio_cfg_default(PIN_GPS_RESET);
    nrf_gpio_cfg_default(PIN_GPS_EN);
    nrf_gpio_cfg_default(GPS_TX_PIN);
    nrf_gpio_cfg_default(GPS_RX_PIN);
}