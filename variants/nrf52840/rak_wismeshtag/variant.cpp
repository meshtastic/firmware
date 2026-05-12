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
#include "FreeRTOS.h"
#include "nrf.h"
#include "power/PowerHAL.h"
#include "sleep.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

const uint32_t g_ADigitalPinMap[] = {
    // P0
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

    // P1
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47};

void initVariant()
{
    // LED1 & LED2
    pinMode(PIN_LED1, OUTPUT);
    ledOff(PIN_LED1);

    // 3V3 Power Rail
    pinMode(PIN_3V3_EN, OUTPUT);
    digitalWrite(PIN_3V3_EN, HIGH);
}

#ifdef LOW_VDD_SYSTEMOFF_DELAY_MS
void variant_nrf52LoopHook(void)
{
    // If VDD stays unsafe for a while (brownout), force System OFF.
    // Skip when VBUS present to allow recovery while USB-powered.
    if (!powerHAL_isVBUSConnected()) {
        // Rate-limit VDD safety checks: powerHAL_isPowerLevelSafe() calls getVDDVoltage() each time.
        static constexpr uint32_t POWER_LEVEL_CHECK_INTERVAL_MS = 100;
        static uint32_t last_vdd_check_ms = 0;
        static bool last_power_level_safe = true;

        const uint32_t now = millis();
        if (last_vdd_check_ms == 0 || (uint32_t)(now - last_vdd_check_ms) >= POWER_LEVEL_CHECK_INTERVAL_MS) {
            last_vdd_check_ms = now;
            last_power_level_safe = powerHAL_isPowerLevelSafe();
        }

        // Do not use millis()==0 as a sentinel: at boot, millis() may be 0 while VDD is unsafe.
        static bool low_vdd_timer_armed = false;
        static uint32_t low_vdd_since_ms = 0;

        if (!last_power_level_safe) {
            if (!low_vdd_timer_armed) {
                low_vdd_since_ms = now;
                low_vdd_timer_armed = true;
            }
            if ((uint32_t)(now - low_vdd_since_ms) >= (uint32_t)LOW_VDD_SYSTEMOFF_DELAY_MS) {
                cpuDeepSleep(portMAX_DELAY);
            }
        } else {
            low_vdd_timer_armed = false;
        }
    }
}
#endif
