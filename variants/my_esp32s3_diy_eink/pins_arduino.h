#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

#define EXTERNAL_NUM_INTERRUPTS 46
#define NUM_DIGITAL_PINS 48
#define NUM_ANALOG_INPUTS 20

#define analogInputToDigitalPin(p) (((p) < 20) ? (analogChannelToDigitalPin(p)) : -1)
#define digitalPinToInterrupt(p) (((p) < 48) ? (p) : -1)
#define digitalPinHasPWM(p) (p < 46)

// The default Wire will be mapped to PMU and RTC
static const uint8_t SDA = 18;
static const uint8_t SCL = 17;

// Default SPI will be mapped to Radio
static const uint8_t MISO = 3;
static const uint8_t SCK = 5;
static const uint8_t MOSI = 6;
static const uint8_t SS = 7;

//#define SPI_MOSI                    (11)
//#define SPI_SCK                     (14)
//#define SPI_MISO                    (2)
//#define SPI_CS                      (13)

//#define SDCARD_CS                   SPI_CS

#endif /* Pins_Arduino_h */
