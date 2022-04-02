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

#ifndef _VARIANT_NORDIC_PCA10059_
#define _VARIANT_NORDIC_PCA10059_

#define PCA10059

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO // Board uses 32khz crystal for LF
// define USE_LFRC    // Board uses RC for LF

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
#define NUM_ANALOG_INPUTS (6)
#define NUM_ANALOG_OUTPUTS (0)

// LEDs
#define PIN_LED1 (0 + 12) //LED1		P1.15
#define PIN_LED2 (0 + 6) //LED2		P1.10

//#define PIN_LED2 (32 + 9) //LED2		P1.10

//RGB NeoPixel LED2
//#define PIN_LED1 (0 + 8) Red
//#define PIN_LED1 (32 + 9) Green
//#define PIN_LED1 (0 + 12) Blue

//Green LED1
//#define PIN_LED1 (0 + 6) //LED1		P1.15

#define LED_BUILTIN PIN_LED1
#define LED_CONN PIN_LED2

#define LED_GREEN PIN_LED1
#define LED_BLUE PIN_LED2

//#define LED_STATE_ON 1 // State when LED is litted
#define LED_STATE_ON 0 // State when LED is litted

/*
 * Buttons
 */

#ifdef PCA10059
#define PIN_BUTTON1 (32 + 6) //BTN_DN		P1.06 Built in button

//Pullup only for external button ?
//#define PIN_BUTTON1 (32 + 10) //BTN_External
//#define BUTTON_NEED_PULLUP

//External Button
//#define PIN_BUTTON1 (32 + 13) //BTN_External
//#define BUTTON_NEED_PULLUP

#endif
//#define PIN_BUTTON2 12
//#define PIN_BUTTON3 24
//#define PIN_BUTTON4 25


/*
 * Analog pins
 */
#define PIN_A0 (-1)  
//#define PIN_A1 (31)
//#define PIN_A2 (28)
//#define PIN_A3 (29)
//#define PIN_A4 (30)
//#define PIN_A5 (31)
//#define PIN_A6 (0xff)
//#define PIN_A7 (0xff)

static const uint8_t A0 = PIN_A0;
//static const uint8_t A1 = PIN_A1;
//static const uint8_t A2 = PIN_A2;
//static const uint8_t A3 = PIN_A3;
//static const uint8_t A4 = PIN_A4;
//static const uint8_t A5 = PIN_A5;
//static const uint8_t A6 = PIN_A6;
//static const uint8_t A7 = PIN_A7;
#define ADC_RESOLUTION 14

// Other pins
#define PIN_AREF (-1)  //AREF		P0.31
//#define PIN_NFC1 (9)
//#define PIN_NFC2 (10)

static const uint8_t AREF = PIN_AREF;

/*
 * Serial interfaces
 */
#define PIN_SERIAL1_RX (-1)
#define PIN_SERIAL1_TX (-1)

// Connected to Jlink CDC
#define PIN_SERIAL2_RX (-1) //RX		P0.24
#define PIN_SERIAL2_TX (-1) //TX		P0.25

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 2

#define PIN_SPI_MISO  (0 + 17)   	    //MISO		  P0.17
#define PIN_SPI_MOSI  (0 + 15)   	    //MOSI		  P0.15
#define PIN_SPI_SCK   (0 + 13)   	    //SCK		    P0.13

#define PIN_SPI1_MISO (-1)            //
#define PIN_SPI1_MOSI (10)            //EPD_MOSI	P0.10
#define PIN_SPI1_SCK  (9)             //EPD_SCLK	P0.09

static const uint8_t SS = (0 + 31);  	//LORA_CS	  P0.31
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

  /*
 * eink display pins
 */

//#define PIN_EINK_EN (-1)              //Not wired ? -1 Try Reset line ?

#define PIN_EINK_EN (0 + 6)         //Use the Green built in LED1

#define PIN_EINK_CS (32)              //EPD_CS		

#define PIN_EINK_BUSY (20)            //EPD_BUSY  	
#define PIN_EINK_DC (24)              //EPD_D/C		

//#define PIN_EINK_RES (22)             //EPD_RES		P0.22  or -1 ?
#define PIN_EINK_RES (-1)

#define PIN_EINK_SCLK (9)             //EPD_SCLK	
#define PIN_EINK_MOSI (10)            //EPD_MOSI	

// Controls power for the eink display - Board power is enabled either by VBUS from USB or the CPU asserting PWR_ON
// FIXME - I think this is actually just the board power enable - it enables power to the CPU also 
//#define PIN_EINK_PWR_ON (0 + 3)


#define HAS_EINK

/*
 * Wire Interfaces
 */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (32 + 4)         //SDA		
#define PIN_WIRE_SCL (32 + 7)         //SCL		

//QSPI Pins
//#define PIN_QSPI_SCK 3
//#define PIN_QSPI_CS 26
//#define PIN_QSPI_IO0 30
//#define PIN_QSPI_IO1 29
//#define PIN_QSPI_IO2 28
//#define PIN_QSPI_IO3 2

// On-board QSPI Flash
//#define EXTERNAL_FLASH_DEVICES IS25LP080D
//#define EXTERNAL_FLASH_USE_QSPI

/* @note RAK5005-O GPIO mapping to RAK4631 GPIO ports
   RAK5005-O <->  nRF52840
   IO1       <->  P0.17 (Arduino GPIO number 17)
   IO2       <->  P1.02 (Arduino GPIO number 34)
   IO3       <->  P0.21 (Arduino GPIO number 21)
   IO4       <->  P0.04 (Arduino GPIO number 4)
   IO5       <->  P0.09 (Arduino GPIO number 9)
   IO6       <->  P0.10 (Arduino GPIO number 10)
   SW1       <->  P0.01 (Arduino GPIO number 1)
   A0        <->  P0.04/AIN2 (Arduino Analog A2
   A1        <->  P0.31/AIN7 (Arduino Analog A7
   SPI_CS    <->  P0.26 (Arduino GPIO number 26) 
 */

// NORDIC_PCA10059 LoRa module
#define USE_SX1262
#define SX126X_CS     (0 + 31)   	//LORA_CS	P0.31
#define SX126X_DIO1   (0 + 29)   	//DIO1		P0.29

#define SX126X_BUSY   (0 + 2)   	//LORA_BUSY	P1.02
#define SX126X_RESET  (32 + 15)   //LORA_RESET	P1.15

//#define SX126X_TXEN   (32 + 13)  	//TXEN		P1.13
//#define SX126X_RXEN   (32 + 10)  	//RXEN		P1.10

#define SX126X_TXEN   (-1)  	//TXEN		P1.13
#define SX126X_RXEN   (-1)  	//RXEN		P1.10

#define SX126X_E22    //DIO2 controlls an antenna switch and the TCXO voltage is controlled by DIO3

// RAK1910 GPS module
// If using the wisblock GPS module and pluged into Port A on WisBlock base
// IO1 is hooked to PPS (pin 12 on header) = gpio 17
// IO2 is hooked to GPS RESET = gpio 34, but it can not be used to this because IO2 is ALSO used to control 3V3_S power (1 is on).
// Therefore must be 1 to keep peripherals powered
// Power is on the controllable 3V3_S rail
// #define PIN_GPS_RESET (34)
#define PIN_GPS_EN (-1)
#define PIN_GPS_PPS (-1) // Pulse per second input from the GPS

#ifdef PCA10059
#define GPS_RX_PIN PIN_SERIAL1_RX
#define GPS_TX_PIN PIN_SERIAL1_TX
#endif

// Battery
// The battery sense is hooked to pin A0 (5)
#define BATTERY_PIN PIN_A0
// and has 12 bit resolution
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
// Definition of milliVolt per LSB => 3.0V ADC range and 12-bit ADC resolution = 3000mV/4096
#define VBAT_MV_PER_LSB (0.73242188F)
// Voltage divider value => 1.5M + 1M voltage divider on VBAT = (1.5M / (1M + 1.5M))
#define VBAT_DIVIDER (0.4F)
// Compensation factor for the VBAT divider
#define VBAT_DIVIDER_COMP (1.73)
// Fixed calculation of milliVolt from compensation value
#define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER VBAT_DIVIDER_COMP //REAL_VBAT_MV_PER_LSB
#define VBAT_RAW_TO_SCALED(x) (REAL_VBAT_MV_PER_LSB * x)

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
