#ifndef _VARIANT_RAK11720_
#define _VARIANT_RAK11720_

/*----------------------------------------------------------------------------
 *        Definitions
 *----------------------------------------------------------------------------*/

// TODO

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

extern const uint32_t g_ADigitalPinMap[];
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))

#define portOutputRegister(port) (&AM_REGVAL(port + offsetof(GPIO_Type, WTA)))
#define portInputRegister(port) ((volatile uint32_t *)&AM_REGVAL(port + offsetof(GPIO_Type, RDA)))
#define portModeRegister(port) ()
#define digitalPinHasPWM(P) (g_ADigitalPinMap[P] > 1) // FIXME
#define digitalPinToBitMask(P) ((uint32_t)0x1 << (P % 32))
#define digitalPinToPinName(P) g_ADigitalPinMap[P]

#define digitalPinToPort(P) (GPIO_BASE + ((P & 0x20) >> 3))

// Interrupts
#define digitalPinToInterrupt(P) (P) // FIXME

/*----------------------------------------------------------------------------
 *        Pins
 *----------------------------------------------------------------------------*/

#define PINS_COUNT (50u)
#define NUM_DIGITAL_PINS (0u)
#define NUM_ANALOG_INPUTS (0u)
#define NUM_ANALOG_OUTPUTS (0u)

#define P44 44 // LED1
#define P45 45 // LED2

#define P39 39 // UART0_TX
#define P40 40 // UART0_RX
#define P42 42 // UART1_TX
#define P43 43 // UART1_RX

#define P25 25 // I2C2_SDA
#define P27 27 // I2C2_SCL

#define P1 1 // SPI0_NSS
#define P5 5 // SPI0_SCK
#define P6 6 // SPI0_MISO
#define P7 7 // SPI0_MOSI

#define P20 20 // SWDIO
#define P21 21 // SWCLK
#define P41 41 // BOOT0 - SWO

// GP4 - GP36 - GP37 - GP38 - GP44(LED1) -GP45(LED2)
// ADC9(12), ADC8(13), ADC3(31), ADC4(32), ADC5(33)
#define P38 38 // IO1
#define P4 4   // IO2
#define P37 37 // IO3
#define P31 31 // IO4 - ADC3(31)
#define P12 12 // IO5 - ADC9(12)
#define P36 36 // IO6
#define P32 32 // IO7 - ADC4(32)

#define P13 13 // AN0 - ADC8(13)
#define P33 33 // AN1 - ADC5(33)

#define P18 18 // ANT_SW(LORA internal)
#define P17 17 // NRESET(LORA internal)
#define P16 16 // BUSY(LORA internal)
#define P15 15 // DIO1(LORA internal)
#define P14 14 // DIO2(LORA internal)
#define P11 11 // SPI_NSS(LORA internal)
#define P8 8   // SPI_CLK(LORA internal)
#define P10 10 // SPI_MOSI(LORA internal)
#define P9 9   // SPI_MISO(LORA internal)

/*
 * WisBlock Base GPIO definitions
 */

#define WB_IO1 P38 // SLOT_A SLOT_B
#define WB_IO2 P4  // SLOT_A SLOT_B
#define WB_IO3 P37 // SLOT_C
#define WB_IO4 P31 // SLOT_C
#define WB_IO5 P12 // SLOT_D
#define WB_IO6 P36 // SLOT_D
#define WB_IO7 P32
#define WB_SW1 0xFF      // IO_SLOT
#define WB_A0 P13        // IO_SLOT
#define WB_A1 P33        // IO_SLOT
#define WB_I2C1_SDA P25  // SENSOR_SLOT IO_SLOT
#define WB_I2C1_SCL P27  // SENSOR_SLOT IO_SLOT
#define WB_I2C2_SDA 0xFF // IO_SLOT
#define WB_I2C2_SCL 0xFF // IO_SLOT
#define WB_SPI_CS P1     // IO_SLOT
#define WB_SPI_CLK P5    // IO_SLOT
#define WB_SPI_MISO P6   // IO_SLOT
#define WB_SPI_MOSI P7   // IO_SLOT
#define WB_RXD0 P40      // IO_SLOT
#define WB_TXD0 P39      // IO_SLOT
#define WB_RXD1 P43      // SLOT_A IO_SLOT
#define WB_TXD1 P42      // SLOT_A IO_SLOT
#define WB_LED1 P44      // IO_SLOT
#define WB_LED2 P45      // IO_SLOT

// LEDs
#define PIN_LED1 WB_LED1 // PA0
#define PIN_LED2 WB_LED2 // PA1

#define LED_BUILTIN PIN_LED1
#define LED_CONN PIN_LED2

#define LED_GREEN PIN_LED1
#define LED_BLUE PIN_LED2

#define LED_STATE_ON 1 // State when LED is litted

/*
 * Analog pins
 */
#define PIN_A0 P13
#define PIN_A1 P33

#define PIN_A3 P5  // channel1
#define PIN_A4 P31 // channel2
#define PIN_A5 P32 // channel4
#define PIN_A6 P36
#define PIN_A7 P7 // channel6

#define ADC_RESOLUTION 14
// Other pins
#define PIN_AREF (0)

static const uint8_t AREF = PIN_AREF;

/*
 * Serial interfaces
 */
#define PIN_SERIAL0_RX WB_RXD0 // PB7
#define PIN_SERIAL0_TX WB_TXD0 // PB6

#define PIN_SERIAL1_RX WB_RXD1 // PA3
#define PIN_SERIAL1_TX WB_TXD1 // PA2

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 1
#define VARIANT_SPI_INTFCS SPI_INTERFACES_COUNT

#define PIN_SPI_CS WB_SPI_CS
#define PIN_SPI_MISO WB_SPI_MISO
#define PIN_SPI_MOSI WB_SPI_MOSI
#define PIN_SPI_SCK WB_SPI_CLK

#define VARIANT_SPI_SDI PIN_SPI_MISO
#define VARIANT_SPI_SDO PIN_SPI_MOSI
#define VARIANT_SPI_CLK PIN_SPI_SCK

static const uint8_t SS = PIN_SPI_CS;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

/*
 * Wire Interfaces
 */
#define WIRE_INTERFACES_COUNT 1
#define VARIANT_WIRE_INTFCS WIRE_INTERFACES_COUNT

#define PIN_WIRE_SDA WB_I2C1_SDA
#define PIN_WIRE_SCL WB_I2C1_SCL

#define VARIANT_Wire_SDA PIN_WIRE_SDA
#define VARIANT_Wire_SCL PIN_WIRE_SCL

#ifdef __cplusplus
}
#endif

#endif /* _VARIANT_RAK11720_ */