#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include "soc/soc_caps.h"
#include <stdint.h>

#define WIFI_LoRa_32_V3 true
#define DISPLAY_HEIGHT 80
#define DISPLAY_WIDTH 160

#define USB_VID 0x303a
#define USB_PID 0x1001

#define EXTERNAL_NUM_INTERRUPTS 46
#define NUM_DIGITAL_PINS 48
#define NUM_ANALOG_INPUTS 20

static const uint8_t LED_BUILTIN = 18;
#define BUILTIN_LED LED_BUILTIN // backward compatibility
#define LED_BUILTIN LED_BUILTIN

#define analogInputToDigitalPin(p) (((p) < 20) ? (analogChannelToDigitalPin(p)) : -1)
#define digitalPinToInterrupt(p) (((p) < 48) ? (p) : -1)
#define digitalPinHasPWM(p) (p < 46)

static const uint8_t TX = 43;
static const uint8_t RX = 44;

static const uint8_t SDA = 5;
static const uint8_t SCL = 6;

static const uint8_t SS = 8;
static const uint8_t MOSI = 10;
static const uint8_t MISO = 11;
static const uint8_t SCK = 9;

static const uint8_t A0 = 1;
static const uint8_t A1 = 2;
static const uint8_t A2 = 3;
static const uint8_t A3 = 4;
static const uint8_t A4 = 5;
static const uint8_t A5 = 6;
static const uint8_t A6 = 7;
static const uint8_t A7 = 8;
static const uint8_t A8 = 9;
static const uint8_t A9 = 10;
static const uint8_t A10 = 11;
static const uint8_t A11 = 12;
static const uint8_t A12 = 13;
static const uint8_t A13 = 14;
static const uint8_t A14 = 15;
static const uint8_t A15 = 16;
static const uint8_t A16 = 17;
static const uint8_t A17 = 18;
static const uint8_t A18 = 19;
static const uint8_t A19 = 20;

static const uint8_t T1 = 1;
static const uint8_t T2 = 2;
static const uint8_t T3 = 3;
static const uint8_t T4 = 4;
static const uint8_t T5 = 5;
static const uint8_t T6 = 6;
static const uint8_t T7 = 7;
static const uint8_t T8 = 8;
static const uint8_t T9 = 9;
static const uint8_t T10 = 10;
static const uint8_t T11 = 11;
static const uint8_t T12 = 12;
static const uint8_t T13 = 13;
static const uint8_t T14 = 14;

static const uint8_t Vext = 36;
static const uint8_t LED = 18;

static const uint8_t RST_LoRa = 12;
static const uint8_t BUSY_LoRa = 13;
static const uint8_t DIO0 = 14;

#endif /* Pins_Arduino_h */
