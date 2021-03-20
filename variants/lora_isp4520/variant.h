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

#ifndef _VARIANT_LORA_ISP4520_
#define _VARIANT_LORA_ISP4520_

#define HW_VERSION_US 1
#undef HW_VERSION
#define HW_VERSION "1.0"

#define USE_SEGGER
/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#define USE_LFXO

//#define USE_SEGGER

// Number of pins defined in PinDescription array
#define PINS_COUNT (16)
#define NUM_DIGITAL_PINS (16)
#define NUM_ANALOG_INPUTS (1) 
#define NUM_ANALOG_OUTPUTS (1)

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 1

// These are in arduino pin numbers, 
// translation in g_ADigitalPinMap in variants.cpp
#define PIN_SPI_MISO (0)
#define PIN_SPI_MOSI (9)
#define PIN_SPI_SCK (2)

/*
 * Wire Interfaces (I2C)
 */
#define WIRE_INTERFACES_COUNT 0

// GPIOs the SX1262 is connected
#define SX1262_CS 1    // aka SPI_NSS
#define SX1262_DIO1 (4)
#define SX1262_BUSY (5) 
#define SX1262_RESET (6)

/*
 * Serial interfaces
 */
#define PIN_SERIAL_RX (10)
#define PIN_SERIAL_TX (11)
// LEDs
#define PIN_LED1 (12)
#define PIN_LED2 (13)
#define PIN_BUZZER (14)

#define LED_BUILTIN PIN_LED1
#define LED_CONN PIN_LED2

#define LED_RED PIN_LED1
#define LED_BLUE PIN_LED2

#define LED_STATE_ON 1 // State when LED is litted

/*
 * Buttons
 */
#define PIN_BUTTON1 (15) 
#define PIN_BUTTON2 (7)
#define PIN_BUTTON3 (8)

// ADC pin and voltage divider
#define BATTERY_PIN 3
#define ADC_MULTIPLIER 1.436

#define SX1262_E22 // Not really an E22 but this board clones using DIO3 for tcxo control

#define NO_WIRE
#define NO_GPS
#define NO_SCREEN
#endif
