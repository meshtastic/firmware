#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x303A
#define USB_PID 0x1001

static const uint8_t TX = 43;
static const uint8_t RX = 44;

static const uint8_t SDA = 17;
static const uint8_t SCL = 18;

static const uint8_t SS = 10;
static const uint8_t MOSI = 11;
static const uint8_t MISO = 14;
static const uint8_t SCK = 12;

#define LED_PIN 15
#define BUTTON_PIN 0

#endif