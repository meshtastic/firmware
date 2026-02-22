#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include "soc/soc_caps.h"
#include <stdint.h>

#define USB_VID 0x303a // USB JTAG/serial debug unit ID
#define USB_PID 0x1001 // USB JTAG/serial debug unit ID

static const uint8_t SS = 5;
static const uint8_t SDA = 8;
static const uint8_t SCL = 9;
static const uint8_t ADC = 10;
static const uint8_t TXD2 = 13;
static const uint8_t MOSI = 14;
static const uint8_t RXD2 = 15;
static const uint8_t MISO = 39;
static const uint8_t SCK = 40;

#endif
