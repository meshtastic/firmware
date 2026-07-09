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

#define LR1121_IRQ_PIN LORA_DIO1
#define LR1121_NRESET_PIN LORA_RESET
#define LR1121_BUSY_PIN LORA_DIO2
#define LR1121_SPI_NSS_PIN LORA_CS
#define LR1121_SPI_SCK_PIN LORA_SCK
#define LR1121_SPI_MOSI_PIN LORA_MOSI
#define LR1121_SPI_MISO_PIN LORA_MISO

// Caps for the LR1121 *chip* output. The external analog PA (driven via the APC2
// DAC pin, see RADIO_PA_APC2_PIN below) provides the rest of the gain, so the chip
// itself is driven low. See src/platform/extra_variants/radiomaster_nomad_gemini.
#define LR1110_MAX_POWER 5
// 2.4G Part
#define LR1120_MAX_POWER 5

// not yet implemented
#define JANUS_RADIO
#define LR1121_IRQ2_PIN 34
#define LR1121_NRESET2_PIN 21
#define LR1121_BUSY2_PIN 39
#define LR1121_SPI_NSS2_PIN 13

// TODO: check if this is correct
// #define LR11X0_DIO3_TCXO_VOLTAGE 1.6
#define LR11X0_DIO_AS_RF_SWITCH

#define HAS_NEOPIXEL                         // Enable the use of neopixels
#define NEOPIXEL_COUNT 2                     // How many neopixels are connected
#define NEOPIXEL_DATA 22                     // GPIO pin used to send data to the neopixels
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800) // Type of neopixels in use
#define ENABLE_AMBIENTLIGHTING               // Turn on Ambient Lighting

// Primary button is GPIO14 (ELRS "button"); GPIO12 is the second button ("button2").
// NOTE: GPIO34 is the second radio's DIO1 (LR1121_IRQ2_PIN) and is input-only with no
// internal pull resistor, so it must NOT be reused as a button.
#define BUTTON_PIN 14
#define BUTTON_NEED_PULLUP

#undef EXT_NOTIFY_OUT

#define RADIO_FAN_EN 2

// Analog PA bias control (ELRS APC2). GPIO26 = ESP32 DAC channel 2, driven with
// dacWrite() (0-255 -> 0-3.3V). Defining this enables the external-PA driver in
// src/platform/extra_variants/radiomaster_nomad_gemini/variant.cpp.
#define RADIO_PA_APC2_PIN 26
