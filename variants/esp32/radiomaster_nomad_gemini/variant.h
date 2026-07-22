#pragma once

#define HAS_SCREEN 0
#define HAS_WIRE 0
#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

#define PIN_SPI_MISO 33
#define PIN_SPI_MOSI 32
#define PIN_SPI_SCK 25
#define PIN_SPI_NSS 27

#define LORA_RESET 15
#define LORA_DIO1 37
#define LORA_DIO2 36
#define LORA_SCK PIN_SPI_SCK
#define LORA_MISO PIN_SPI_MISO
#define LORA_MOSI PIN_SPI_MOSI
#define LORA_CS PIN_SPI_NSS

// supported modules list
#define USE_LR1121
#define LR1121_SUBGHZ_ONLY

#define LR1121_IRQ_PIN LORA_DIO1
#define LR1121_NRESET_PIN LORA_RESET
#define LR1121_BUSY_PIN LORA_DIO2
#define LR1121_SPI_NSS_PIN LORA_CS
#define LR1121_SPI_SCK_PIN LORA_SCK
#define LR1121_SPI_MOSI_PIN LORA_MOSI
#define LR1121_SPI_MISO_PIN LORA_MISO

// The calibrated external PA supplies the remaining gain above the LR1121 output.
#define LR1110_MAX_POWER 5

// The second LR1121 is held inactive until dual-radio support is implemented.
#define NOMAD_SECOND_RADIO_NSS_PIN 13
#define NOMAD_SECOND_RADIO_NRESET_PIN 21
#define NOMAD_WIFI_BACKPACK_NRESET_PIN 19

#define LR11X0_DIO_AS_RF_SWITCH

#define HAS_NEOPIXEL                         // Enable the use of neopixels
#define NEOPIXEL_COUNT 2                     // How many neopixels are connected
#define NEOPIXEL_DATA 22                     // GPIO pin used to send data to the neopixels
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800) // Type of neopixels in use
#define ENABLE_AMBIENTLIGHTING               // Turn on Ambient Lighting

// Primary button is GPIO14; GPIO12 is the second hardware button.
#define BUTTON_PIN 14
#define BUTTON_NEED_PULLUP

#undef EXT_NOTIFY_OUT

#define RADIO_FAN_EN 2

#define RADIO_EXTERNAL_PA
#define RADIO_PA_APC2_PIN 26
