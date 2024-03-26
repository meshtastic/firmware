// Need this file for ESP32-S3
// No need to modify this file, changes to pins imported from variant.h
// Most is similar to https://github.com/espressif/arduino-esp32/blob/master/variants/esp32s3/pins_arduino.h

#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>
#include <variant.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

#define EXTERNAL_NUM_INTERRUPTS 46
#define NUM_DIGITAL_PINS 48
#define NUM_ANALOG_INPUTS 20

#define analogInputToDigitalPin(p) (((p) < 20) ? (analogChannelToDigitalPin(p)) : -1)
#define digitalPinToInterrupt(p)                                                                                                 \
    (((p) < 48) ? (p) : -1) // Maybe it should be <= 48 but this is from a trustworthy source so it is likely correct
#define digitalPinHasPWM(p) (p < 46)

// Serial
static const uint8_t TX = UART_TX;
static const uint8_t RX = UART_RX;

// Default SPI will be mapped to Radio
static const uint8_t SS = LORA_CS;
static const uint8_t SCK = LORA_SCK;
static const uint8_t MOSI = LORA_MOSI;
static const uint8_t MISO = LORA_MISO;

// The default Wire will be mapped to PMU and RTC
static const uint8_t SCL = I2C_SCL;
static const uint8_t SDA = I2C_SDA;

#endif /* Pins_Arduino_h */
