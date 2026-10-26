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
#include "Power.h"
#include "nrf.h"
#include "wiring_constants.h"
#include "wiring_digital.h"
#include <Arduino.h>
#include <nrf_sdm.h>
#include <nrf_soc.h>

// GPREGRET2 survives a soft reset but is cleared on power-on/brownout
#define LOWBAT_DUTY_MAGIC 0xB5

void nrf52FlashQuiesce();

static TaskHandle_t lowBatWaitTask;

static void lowBatWakeIsr()
{
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(lowBatWaitTask, &woken);
    portYIELD_FROM_ISR(woken);
}

static uint32_t readBatteryMv()
{
    pinMode(ADC_CTRL, OUTPUT);
    digitalWrite(ADC_CTRL, ADC_CTRL_ENABLED);
    delay(10);
    analogReference(VBAT_AR_INTERNAL);
    analogReadResolution(BATTERY_SENSE_RESOLUTION_BITS);
    uint32_t raw = 0;
    for (int i = 0; i < BATTERY_SENSE_SAMPLES; i++)
        raw += analogRead(BATTERY_PIN);
    digitalWrite(ADC_CTRL, !ADC_CTRL_ENABLED);
    return (float)raw / BATTERY_SENSE_SAMPLES * (AREF_VOLTAGE * 1000.0F / BATTERY_SENSE_RESOLUTION) * ADC_MULTIPLIER;
}

// Low-battery recheck, armed by variant_shutdown(). Each boot measures VBAT once: if still low, blink the
// power LED, idle SOLAR_LOWBAT_RECHECK_SECS and reset to recheck; otherwise clear the flag and reset into a
// normal boot. Button or USB during the idle also boots normally. No LOG_* here: runs before consoleInit().
void earlyInitVariant()
{
    if (NRF_POWER->GPREGRET2 != LOWBAT_DUTY_MAGIC)
        return;

    const uint16_t ocv[NUM_OCV_POINTS] = {OCV_ARRAY};

    // Keep peripherals unpowered; the reset at the end restores all pin states
    pinMode(PIN_POWER_EN, OUTPUT);
    digitalWrite(PIN_POWER_EN, LOW);
    pinMode(PIN_GPS_EN, OUTPUT);
    digitalWrite(PIN_GPS_EN, LOW);
    pinMode(VDD_FLASH_EN, OUTPUT);
    digitalWrite(VDD_FLASH_EN, LOW);
    pinMode(EXT_PWR_DETECT, INPUT);
    pinMode(EXT_CHRG_DETECT, INPUT);
    pinMode(PIN_BUTTON1, INPUT_PULLUP);

    // An active solar charger lifts VBAT above its open-circuit voltage
    uint32_t threshold = ocv[NUM_OCV_POINTS - 1] + SOLAR_WAKE_THRESHOLD;
    if (digitalRead(EXT_CHRG_DETECT) == EXT_CHRG_DETECT_VALUE)
        threshold += SOLAR_CHARGE_LIFT;

    if (readBatteryMv() < threshold && digitalRead(EXT_PWR_DETECT) != HIGH) {
        // Button or USB plug-in ends the idle wait early
        lowBatWaitTask = xTaskGetCurrentTaskHandle();
        ulTaskNotifyTake(pdTRUE, 0);
        attachInterrupt(PIN_BUTTON1, lowBatWakeIsr, FALLING);
        attachInterrupt(EXT_PWR_DETECT, lowBatWakeIsr, RISING);
        // Edge interrupts miss a level already active at attach time
        if (digitalRead(PIN_BUTTON1) == LOW || digitalRead(EXT_PWR_DETECT) == HIGH)
            xTaskNotifyGive(lowBatWaitTask);

        // LED_POWER was switched on in setup(); keep it visible ~0.3 s as a liveness blink
        delay(300);
        digitalWrite(LED_POWER, LED_STATE_OFF);
        // Timeout keeps the flag set so the next boot rechecks; button/USB fall through to a normal boot
        if (!ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(SOLAR_LOWBAT_RECHECK_SECS * 1000UL)))
            NVIC_SystemReset();
    }

    NRF_POWER->GPREGRET2 = 0;
    NVIC_SystemReset();
}

const uint32_t g_ADigitalPinMap[] = {
    // P0 - pins 0 and 1 are hardwired for xtal and should never be enabled
    0xff, 0xff, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

    // P1
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47};

void initVariant()
{
    pinMode(LED_PAIRING, OUTPUT);
    ledOff(LED_PAIRING);

    pinMode(VDD_FLASH_EN, OUTPUT);
    digitalWrite(VDD_FLASH_EN, HIGH);
}

// called from main-nrf52.cpp during the cpuDeepSleep() function
void variant_shutdown()
{
    // Low battery: reset into the earlyInitVariant() periodic recheck instead of System OFF
    if (power && power->isLowBattery()) {
        uint8_t sdEnabled = 0;
        sd_softdevice_is_enabled(&sdEnabled);
        if (sdEnabled) {
            sd_power_gpregret_clr(1, 0xFF);
            sd_power_gpregret_set(1, LOWBAT_DUTY_MAGIC);
        } else {
            NRF_POWER->GPREGRET2 = LOWBAT_DUTY_MAGIC;
        }
        nrf52FlashQuiesce();
        NVIC_SystemReset();
    }

    // This sets the pin to OUTPUT and LOW for the pins *not* in the if block.
    for (int pin = 0; pin < 48; pin++) {
        if (pin == PIN_GPS_EN || pin == ADC_CTRL || pin == PIN_BUTTON1 || pin == EXT_PWR_DETECT || pin == BATTERY_PIN ||
            pin == PIN_SPI_MISO || pin == PIN_SPI_MOSI || pin == PIN_SPI_SCK || pin == SX126X_CS || pin == SX126X_RESET ||
            pin == SX126X_BUSY || pin == SX126X_DIO1) {
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

    // SENSE is level-triggered: arming while USB is present would wake immediately
    if (digitalRead(EXT_PWR_DETECT) == LOW)
        nrf_gpio_cfg_sense_input(EXT_PWR_DETECT, NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_SENSE_HIGH);
    nrf_gpio_cfg_default(BATTERY_PIN);
}
