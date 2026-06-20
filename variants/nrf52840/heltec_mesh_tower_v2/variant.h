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

#ifndef _VARIANT_HELTEC_NRF_
#define _VARIANT_HELTEC_NRF_
/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO // Board uses 32khz crystal for LF

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// Number of pins defined in PinDescription array
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

#define PIN_LED1  (32 + 15) // green
#define LED_BLUE  PIN_LED1  // fake for bluefruit library
#define LED_GREEN PIN_LED1
#define LED_STATE_ON 0 // State when LED is lit

/*
 * Buttons
 */
#define PIN_BUTTON1 (32 + 10) // P1.10, MCU_USER

/*
No longer populated on PCB
*/
#define PIN_SERIAL2_RX (-1)
#define PIN_SERIAL2_TX (-1)

/*
 * I2C
 */

#define WIRE_INTERFACES_COUNT 1

// I2C bus 0, routed to HUSB238 USB PD sink controller.
#define PIN_WIRE_SDA (0 + 30) // P0.30, PD_SINK_SDA
#define PIN_WIRE_SCL (0 + 5)  // P0.05, PD_SINK_SCL

/*
 * Lora radio
 */
#define USE_SX1262
#define SX126X_CS (0 + 24)
#define LORA_CS SX126X_CS
#define SX126X_DIO1 (0 + 20)
#define SX126X_BUSY (0 + 17)
#define SX126X_RESET (0 + 25)
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define USE_KCT8103L_PA_ONLY
#define LORA_KCT8103L_EN      (0 + 15)  // CSD - KCT8103L chip enable (HIGH=on)
#define LORA_KCT8103L_TX_RX   (0 + 16)  // TX or bypass control (HIGH=TX, LOW=RX)
#define LORA_PA_POWER         LORA_KCT8103L_EN
#define RF_PA_DETECT_PIN      (0 + 13)  // HIGH=high-power PA, LOW=low-power
#define RF_PA_HIGH_POWER_VALUE HIGH

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 1

// For LORA, spi 0
#define PIN_SPI_MISO (0 + 23)
#define PIN_SPI_MOSI (0 + 22)
#define PIN_SPI_SCK (0 + 19)

/*
 * GPS pins
 */

#define GPS_L76K

#define PIN_GPS_RESET (32 + 6)
#define GPS_RESET_MODE LOW
#define PIN_GPS_EN (0 + 7) // P0.07, VGNSS_Ctrl
#define GPS_EN_ACTIVE LOW
#define PERIPHERAL_WARMUP_MS 1000 // Make sure GNSS power is stable before continuing
#define PIN_GPS_STANDBY (32 + 2) // P1.02, WAKE_UP. Low allows sleep, high forces wake.
#define PIN_GPS_PPS (32 + 4)     // P1.04, 1PPS
#define GPS_RX_PIN (32 + 5)      // P1.05, MCU RX connected to GPS TXD.
#define GPS_TX_PIN (32 + 7)      // P1.07, MCU TX connected to GPS RXD.

#define GPS_THREAD_INTERVAL 50

#define PIN_SERIAL1_RX GPS_RX_PIN
#define PIN_SERIAL1_TX GPS_TX_PIN

// Hardware watchdog
#define HAS_HARDWARE_WATCHDOG
#define HARDWARE_WATCHDOG_DONE (0 + 9)
#define HARDWARE_WATCHDOG_WAKE (0 + 10)
#define HARDWARE_WATCHDOG_TIMEOUT_MS (6 * 60 * 1000) // 6 minute watchdog

#define SERIAL_PRINT_PORT 0

#define ADC_CTRL (0 + 21)     // P0.21, ADC_Ctrl
#define ADC_CTRL_ENABLED HIGH
#define BATTERY_PIN (0 + 4)   // P0.04, ADC_IN
#define ADC_RESOLUTION 14

#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER (4.916F)

// nRF52840 AIN2 is P0.04 on the MeshTower V2 battery divider.
#define BATTERY_LPCOMP_INPUT NRF_LPCOMP_INPUT_2

// We have AIN2 with a VBAT divider so AIN2 = VBAT * (100/490)
// We have the device going deep sleep under 3.1V, which is AIN2 = 0.63V
// So we can wake up when VBAT>=VDD is restored to 3.3V, where AIN2 = 0.67V
// Ratio 0.67/3.3 = 0.20, so we can pick a bit higher, 2/8 VDD, which means
// VBAT=4.04V
#define BATTERY_LPCOMP_THRESHOLD NRF_LPCOMP_REF_SUPPLY_2_8

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
