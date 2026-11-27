// ExpressLRS "Generic C3 LR1121" RX layout: ESP32-C3 + LR1121, one NeoPixel, one button.

#define BUTTON_PIN 9

#define HAS_NEOPIXEL
#define NEOPIXEL_COUNT 1
#define NEOPIXEL_DATA 8
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800)

#define USE_LR1121
#define LORA_SCK 6
#define LORA_MISO 5
#define LORA_MOSI 4
#define LORA_CS 7
#define LR1121_SPI_NSS_PIN LORA_CS
#define LR1121_NRESET_PIN 2
#define LR1121_BUSY_PIN 3
#define LR1121_IRQ_PIN 1
// No TCXO in the ExpressLRS layout
#define LR11X0_DIO_AS_RF_SWITCH
