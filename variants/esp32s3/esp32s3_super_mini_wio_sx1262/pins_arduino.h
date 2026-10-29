#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x1A86
#define USB_PID 0x55D4

static const uint8_t TX = 43;
static const uint8_t RX = 44;

static const uint8_t SDA = 17;
static const uint8_t SCL = 18;

static const uint8_t SS = 5;
static const uint8_t MOSI = 9;
static const uint8_t MISO = 8;
static const uint8_t SCK = 7;

#define LED_PIN 15
#define BUTTON_PIN 0

#endif