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

#ifndef _VARIANT_ELECROW_EINK_V1_0_
#define _VARIANT_ELECROW_EINK_V1_0_

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

#define PIN_NFC1 (9)
#define PIN_NFC2 (10)

#define CANNED_MESSAGE_MODULE_ENABLE 1
#define PRESET_MESSAGE_MODULE_ENABLE 1

/*Power*/
#define I2C_EN          (0 + 13)
#define VCC_ELNK_EN     (32 + 10) 
#define GPS_EN          (0 + 16)
#define ADC_EN          (32 + 8)


/*Buttons*/
#define HAS_BUTTON              1
#define PIN_BUTTON_E            (0 + 12)
#define PIN_BUTTON_EC04_A       (0 + 8)
#define PIN_BUTTON_EC04_B       (32 + 9)
#define PIN_BUTTON_EC04         (0 + 6)
#define PIN_BUTTON1             PIN_BUTTON_E

/*LED*/
#define PIN_LED1 -1
#define LED_STATE_ON HIGH // State when LED is lit
#define LED_BUILTIN PIN_LED1
#define LED_BLUE PIN_LED1

/*BUZZER*/
#define PIN_BUZZER  (32 + 1)

/*USB_CHECK*/
#define USB_VBUS    (32 + 3)

/*CHARGE_CHECK*/
#define CHRG        (32 + 5)
#define DONE        (32 + 6)

/*Wire Interfaces*/
#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA (0 + 26)
#define PIN_WIRE_SCL (0 + 27)

/*GPS*/
#define HAS_GPS 1
#define GPS_L76K
#define GPS_BAUDRATE 9600
#define PIN_GPS_RESET (0 + 17)
#define PIN_GPS_STANDBY (0 + 15)    // An output to wake GPS, low means allow sleep, high means force wake
#define PIN_SERIAL1_RX (32 + 2)
#define PIN_SERIAL1_TX (32 + 4)
#define GPS_TX_PIN PIN_SERIAL1_TX
#define GPS_RX_PIN PIN_SERIAL1_RX  
#define GPS_THREAD_INTERVAL 50

/*FLASH*/
#define PIN_QSPI_CS (32 + 15)
#define PIN_QSPI_SCK (32 + 14)
#define PIN_QSPI_IO0 (32 + 12) // MOSI if using two bit interface
#define PIN_QSPI_IO1 (32 + 13) // MISO if using two bit interface
#define PIN_QSPI_IO2 (0 + 7)   // WP if using two bit interface (i.e. not used)
#define PIN_QSPI_IO3 (0 + 5)   // HOLD if using two bit interface (i.e. not used)
#define EXTERNAL_FLASH_DEVICES MX25R1635F
#define EXTERNAL_FLASH_USE_QSPI

/*SPI*/
#define SPI_INTERFACES_COUNT 2
#define PIN_SPI_NSS    (0 + 21)    
#define PIN_SPI_SCK    (0 + 19)
#define PIN_SPI_MOSI   (0 + 20)
#define PIN_SPI_MISO   (0 + 22)

#define PIN_SPI1_NSS    (0 + 30)
#define PIN_SPI1_SCK    (0 + 31)
#define PIN_SPI1_MOSI   (0 + 29)
#define PIN_SPI1_MISO   -1
/*EINK*/
#define MESHTASTIC_USE_EINK_UI 1
#define USE_EINK        1
#define PIN_EINK_CS     PIN_SPI1_NSS
#define PIN_EINK_SCLK   PIN_SPI1_SCK
#define PIN_EINK_MOSI   PIN_SPI1_MOSI
#define PIN_EINK_EN     (32 + 11)       //Note: this is really just backlight power
#define PIN_EINK_BUSY   (0 + 3)
#define PIN_EINK_DC     (0 + 28)
#define PIN_EINK_RES    (0 + 2)

/*Lora radio*/
#define USE_SX1262
#define SX1262_CTRL     (0 + 23)
#define SX126X_RESET    (0 + 24) // RST
#define SX126X_DIO1     (0 + 25) // IRQ
#define SX126X_DIO2     (32 + 0)  // BUSY
#define SX126X_SCK      PIN_SPI_SCK
#define SX126X_MISO     PIN_SPI_MISO
#define SX126X_MOSI     PIN_SPI_MOSI
#define SX126X_CS       PIN_SPI_NSS

#define SX1262_IRQ_PIN              SX126X_DIO1
#define SX1262_NRESET_PIN           SX126X_RESET
#define SX126X_BUSY                 SX126X_DIO2
#define SX1262_SPI_NSS_PIN          SX126X_CS
#define SX1262_SPI_SCK_PIN          SX126X_SCK
#define SX1262_SPI_MOSI_PIN         SX126X_MOSI
#define SX1262_SPI_MISO_PIN         SX126X_MISO
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE    3.3

/*RTC*/
#define PCF8563_RTC 0x51

/*Battert*/
#define BATTERY_PIN (0 + 4)
#define ADC_V (0 + 4)
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#define BATTERY_SENSE_SAMPLES 100
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 2.4
#define VBAT_AR_INTERNAL AR_INTERNAL_2_4
#define ADC_MULTIPLIER (1.75)
#define EXT_PWR_DETECT USB_VBUS

#ifdef __cplusplus
}
#endif

#endif