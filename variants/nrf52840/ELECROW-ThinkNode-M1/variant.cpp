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
#include "Throttle.h"
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
// The L76K's wiring; earlyInitVariant() works out the AG3352's.
uint8_t m1GpsTxPin = 32 + 8;
uint8_t m1GpsRxPin = 32 + 9;
static uint16_t m1UartEdges[2]; // falling edges seen on P1.08 and P1.09, for the boot log
static bool m1UartFound;
static bool m1StrapLow;

void initVariant()
{
    // pinMode(PIN_LED1, OUTPUT);
    // ledOff(PIN_LED1);
}

#if !MESHTASTIC_EXCLUDE_GPS
// Out of reset the AG3352 starts talking, and only its TX line toggles; an unpowered L76K stays silent.
// Elecrow's notes cross the AG3352 UART the other way from the L76K's, which no board has confirmed yet.
// No pulls: the lines may run at 1.8 V. Returns whether anything talked.
static bool m1FindAg3352Uart()
{
    constexpr uint8_t pins[2] = {8, 9}; // P1.08, P1.09
    constexpr uint16_t enough = 50;     // a few characters
    constexpr uint32_t timeoutMs = 2000;
    for (uint8_t pin : pins)
        pinMode(32 + pin, INPUT);
    uint32_t last = NRF_P1->IN;
    const uint32_t start = millis();
    while (Throttle::isWithinTimespanMs(start, timeoutMs) && m1UartEdges[0] < enough && m1UartEdges[1] < enough) {
        const uint32_t now = NRF_P1->IN;
        const uint32_t fell = last & ~now;
        for (uint8_t i = 0; i < 2; i++)
            m1UartEdges[i] += (fell >> pins[i]) & 1;
        last = now;
    }
    if (m1UartEdges[0] < enough && m1UartEdges[1] < enough)
        return false; // leave the L76K's pins
    // Only a clear answer overrides the notes: one line busy, the other still.
    const bool rxOnP109 = m1UartEdges[1] >= enough && m1UartEdges[0] <= 2;
    m1UartFound = rxOnP109 || (m1UartEdges[0] >= enough && m1UartEdges[1] <= 2);
    m1GpsRxPin = rxOnP109 ? 32 + 9 : 32 + 8;
    m1GpsTxPin = rxOnP109 ? 32 + 8 : 32 + 9;
    return true;
}
#endif

// Here rather than initVariant(), whose delay() would run before the scheduler. No LOG_* yet.
void earlyInitVariant()
{
    // No internal pull: either one would swamp the AG3352 board's 100K pull-down or the L76K's weak pull-up.
    pinMode(PIN_GPS_VERSION, INPUT);
    delay(2);
    uint8_t high = 0;
    for (uint8_t i = 0; i < 10; i++)
        high += digitalRead(PIN_GPS_VERSION) == HIGH;
    m1StrapLow = high < 3;

#if !MESHTASTIC_EXCLUDE_GPS
    if (m1StrapLow) {
        // The core supply must be up before the reset, or the GPS boots and then stalls without it. A pull-up beats
        // the EN pin's 100K pull-down, and is harmless if this is an L76K board, where P1.01 senses the GPS switch.
        pinMode(GPS_EN_AG3352, INPUT_PULLUP);
        delay(50);
        // The AG3352 stays powered across an MCU reset and may still be in backup mode; only RESETB ends that.
        pinMode(PIN_GPS_RESET, OUTPUT);
        digitalWrite(PIN_GPS_RESET, HIGH); // asserts RESETB through Q16
        delay(10);
        digitalWrite(PIN_GPS_RESET, LOW);
        m1HasAg3352 = m1FindAg3352Uart();
        if (m1HasAg3352) {
            pinMode(GPS_EN_AG3352, OUTPUT);
            digitalWrite(GPS_EN_AG3352, HIGH);
            pinMode(GPS_SLEEP_INT, OUTPUT);
            digitalWrite(GPS_SLEEP_INT, HIGH);
        } else {
            // An L76K board: hand back the switch sense, and REINIT, which held low would keep the L76K in reset.
            pinMode(GPS_EN_AG3352, INPUT);
            pinMode(PIN_GPS_RESET, INPUT);
        }
    }
#else
    m1HasAg3352 = m1StrapLow;
#endif
}

void lateInitVariant()
{
    LOG_INFO("ThinkNode M1 GPS %s", m1HasAg3352 ? "AG3352" : "L76K");
#if !MESHTASTIC_EXCLUDE_GPS
    if (m1StrapLow && !m1HasAg3352)
        LOG_INFO("GPS strap low but no UART traffic: L76K with the GPS switch off (or a silent AG3352)");
    if (m1HasAg3352)
        LOG_INFO("AG3352 UART: MCU RX on P1.%02u, %s (falling edges P1.08 %u, P1.09 %u)", m1GpsRxPin - 32,
                 m1UartFound ? "found" : "not seen, assumed", m1UartEdges[0], m1UartEdges[1]);
#endif
}

void variant_shutdown()
{
#if !MESHTASTIC_EXCLUDE_GPS
    if (m1HasAg3352) {
        pinMode(PIN_GPS_EN, OUTPUT);
        digitalWrite(PIN_GPS_EN, LOW);     // core supply off; the 1.8 V rail keeps the GPS in backup mode
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
        if (m1HasAg3352 && (pin == GPS_SLEEP_INT || pin == PIN_GPS_RESET || pin == PIN_GPS_EN))
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